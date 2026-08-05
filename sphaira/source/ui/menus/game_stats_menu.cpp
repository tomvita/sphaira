#include "ui/menus/game_stats_menu.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "ui/nvg_util.hpp"
#include "utils/devoptab.hpp"
#include "yati/nx/ncm.hpp"
#include <cstring>
#include <ctime>
#include <cstdio>
#include <string>
#include <algorithm>
#include <vector>

namespace sphaira::ui::menu::game {
namespace {

struct NsoBuildIdHeader {
    u8 reserved[0x40];
    u8 build_id[0x20];
};

static_assert(offsetof(NsoBuildIdHeader, build_id) == 0x40);

void FormatBuildId(const NsoBuildIdHeader& header, std::string& out) {
    char build_id[17]{};
    for (size_t i = 0; i < 8; ++i) {
        std::snprintf(build_id + i * 2, sizeof(build_id) - i * 2, "%02X", header.build_id[i]);
    }
    out = build_id;
}

Result ReadBuildIdFromCodeFs(fs::Fs& code_fs, std::string& out) {
    fs::File main;
    R_TRY(code_fs.OpenFile("/main", FsOpenMode_Read, &main));
    NsoBuildIdHeader header{};
    u64 bytes_read{};
    R_TRY(main.Read(0, &header, sizeof(header), 0, &bytes_read));
    R_UNLESS(bytes_read == sizeof(header), Result_GameMultipleKeysFound);
    FormatBuildId(header, out);
    R_SUCCEED();
}

Result ReadBuildIdFromNca(NcmContentStorage* cs, const NcmContentId& content_id, std::string& out) {
    fs::FsPath root;
    R_TRY(devoptab::MountNcaNcm(cs, &content_id, root));
    ON_SCOPE_EXIT(devoptab::UmountNeworkDevice(root));

    char main_path[FS_MAX_PATH]{};
    std::snprintf(main_path, sizeof(main_path), "%s/exeFS/main", root.s);
    FILE* main = std::fopen(main_path, "rb");
    R_UNLESS(main, Result_GameMultipleKeysFound);
    ON_SCOPE_EXIT(std::fclose(main));

    NsoBuildIdHeader header{};
    R_UNLESS(std::fread(&header, 1, sizeof(header), main) == sizeof(header), Result_GameMultipleKeysFound);
    FormatBuildId(header, out);
    R_SUCCEED();
}

Result LoadGameBuildId(const Entry& entry, std::string& out) {
    title::MetaEntries statuses;
    R_TRY(GetMetaEntries(entry, statuses, title::ContentFlag_Nacp));

    // Prefer the newest patch, falling back to the newest base application.
    const NsApplicationContentMetaStatus* selected{};
    for (const auto& status : statuses) {
        if (status.meta_type != NcmContentMetaType_Application && status.meta_type != NcmContentMetaType_Patch) {
            continue;
        }
        if (!selected || status.meta_type > selected->meta_type ||
            (status.meta_type == selected->meta_type && status.version > selected->version)) {
            selected = &status;
        }
    }
    R_UNLESS(selected, Result_GameMultipleKeysFound);

    NcmMetaData meta;
    R_TRY(GetNcmMetaFromMetaStatus(*selected, meta));
    std::vector<NcmContentInfo> infos;
    R_TRY(ncm::GetContentInfos(meta.db, &meta.key, infos));

    for (const auto& info : infos) {
        if (info.content_type != NcmContentType_Program) {
            continue;
        }

        u64 program_id;
        fs::FsPath path;
        R_TRY(ncm::GetFsPathFromContentId(meta.cs, meta.key, info.content_id, &program_id, &path));
        // fsp-ldr isn't available in every homebrew launch context. Try it first,
        // then use Sphaira's own NCA reader, which is already used by View Content.
        if (R_SUCCEEDED(fsldrInitialize())) {
            FsCodeInfo code_info{};
            FsFileSystem raw_fs{};
            const auto rc = fsldrOpenCodeFileSystem(&code_info, program_id,
                static_cast<NcmStorageId>(selected->storageID), path, FsContentAttributes_All, &raw_fs);
            if (R_SUCCEEDED(rc)) {
                fs::FsNative code_fs{&raw_fs, true};
                if (R_SUCCEEDED(ReadBuildIdFromCodeFs(code_fs, out))) {
                    fsldrExit();
                    R_SUCCEED();
                }
            }
            fsldrExit();
        }

        return ReadBuildIdFromNca(meta.cs, info.content_id, out);
    }

    R_THROW(Result_GameMultipleKeysFound);
}

} // namespace

GameStatsMenu::GameStatsMenu(const Entry& entry) : grid::Menu{"", 0}, m_entry(entry) {
    this->SetActions(
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::Y, Action{"Toggle History"_i18n, [this](){
            m_show_full_history = !m_show_full_history;
            InitEntries();
        }}),
        std::make_pair(Button::DOWN, Action{"Scroll"_i18n, [](){}})
    );
    
    std::vector<u8> control_data(sizeof(NsApplicationControlData));
    u64 control_size{};
    if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, m_entry.app_id,
            reinterpret_cast<NsApplicationControlData*>(control_data.data()), control_data.size(), &control_size))) {
        const auto* nacp = reinterpret_cast<const NacpStruct*>(control_data.data());
        m_display_version = nacp->display_version;
    }
    LoadGameBuildId(m_entry, m_build_id);

    InitEntries();
}

GameStatsMenu::~GameStatsMenu() {
    auto vg = App::GetVg();
    for (auto& entry : m_entries) {
        if (entry.image) {
            nvgDeleteImage(vg, entry.image);
        }
    }
}

auto GameStatsMenu::GetShortTitle() const -> const char* {
    return "Stats";
}

// Helper to correct PDM timestamp
static u64 PdmToPosix(u32 seconds) {
    return (u64)seconds;
}

void GameStatsMenu::InitEntries() {
    // this->SetSubHeading(m_entry.GetName());

    // Local copies of stats
    u64 playtime = m_entry.playtime;
    u32 total_launches = m_entry.total_launches;
    u64 last_played = m_entry.last_played;
    auto user_playtimes = m_entry.user_playtimes;
    auto user_launches = m_entry.user_launches;
    auto user_first = m_entry.user_first_played;
    auto user_last = m_entry.user_last_played;

    // Lazy load if stats are missing
    const auto accounts = App::GetAccountList();
    if (user_first.empty() || user_launches.empty()) {
        user_playtimes.clear();
        user_launches.clear();
        user_first.clear();
        user_last.clear();
        playtime = 0;
        total_launches = 0; // Reset to recalculate accurately

        for (const auto& acc : accounts) {
            PdmPlayStatistics stats{};
            u64 u_time = 0;
            u32 u_launch = 0;
            u64 u_first = 0;
            u64 u_last = 0;
            
            if (R_SUCCEEDED(pdmqryQueryPlayStatisticsByApplicationIdAndUserAccountId(m_entry.app_id, acc.uid, true, &stats))) {
                u_time = stats.playtime;
                u_launch = stats.total_launches;
                u_first = PdmToPosix(stats.first_timestamp_user);
                u_last = PdmToPosix(stats.last_timestamp_user);
            }
            
            playtime += u_time;
            total_launches += u_launch;
            
            user_playtimes.push_back(u_time);
            user_launches.push_back(u_launch);
            user_first.push_back(u_first);
            user_last.push_back(u_last);

            if (u_last > last_played) last_played = u_last;
        }

        // Global fallback if no user stats found
        if (playtime == 0) {
            PdmPlayStatistics stats{};
            if (R_SUCCEEDED(pdmqryQueryPlayStatisticsByApplicationId(m_entry.app_id, true, &stats))) {
                playtime = stats.playtime;
                total_launches = stats.total_launches;
                if (user_playtimes.empty()) {
                    user_playtimes.push_back(playtime);
                    user_launches.push_back(total_launches); 
                }
            }
        }
    }

    // Fallback: if user_playtimes is still empty but we have total stats, add a generic entry
    if (user_playtimes.empty() && playtime > 0) {
        user_playtimes.push_back(playtime);
        user_launches.push_back(total_launches);
    }

    // Custom List setup
    if (!m_list) {
        // Position list on the right side
        // X(480) + W(740) = 1220 (Menu edge)
        const Vec4 v{480, 110, 740, 30};
        const Vec2 pad{0, 6};
        // Total height per item: 36. 
        // ~510 available height / 36 = ~14 items
        m_list = std::make_unique<List>(1, 14, m_pos, v, pad);
    }

    // Store general stats
    m_total_playtime = playtime;
    m_total_launches = total_launches;
    m_last_played = last_played;
    m_first_played = 0;
    m_global_launches = 0;

    for (const auto f : user_first) {
        if (f > 0 && (m_first_played == 0 || f < m_first_played)) {
            m_first_played = f;
        }
    }
    
    {
        PdmPlayStatistics global_stats{};
        if (R_SUCCEEDED(pdmqryQueryPlayStatisticsByApplicationId(m_entry.app_id, true, &global_stats))) {
            m_global_launches = global_stats.total_launches;
        }
    }

    m_entries.clear();

    
    // Per-User Playtime or Full History
    if (!user_playtimes.empty()) {
        const char* header = m_show_full_history ? "Detailed Play History:" : "Play Time per Profile:";
        m_entries.emplace_back();
        strncpy(m_entries.back().lang.name, header, sizeof(m_entries.back().lang.name) - 1);
        
        for (size_t i = 0; i < user_playtimes.size(); i++) {
            if (user_playtimes[i] > 0) {
                std::string user_name = (i < accounts.size()) ? accounts[i].nickname : "Profile " + std::to_string(i + 1);
                
                if (m_show_full_history && i < accounts.size()) {
                    m_entries.emplace_back();
                    std::string user_header = "  " + user_name + ":";
                    strncpy(m_entries.back().lang.name, user_header.c_str(), sizeof(m_entries.back().lang.name) - 1);

                    int displayed = 0;
                    s32 total_entries = 0, start_idx = 0, end_idx = 0;
                    if (R_SUCCEEDED(pdmqryGetAvailablePlayEventRange(&total_entries, &start_idx, &end_idx)) && total_entries > 0) {
                        struct GlobalEvent {
                            int type; // 0: launch, 1: infocus, 2: outfocus, 3: exit, 4: active, 5: inactive
                            AccountUid uid;
                            u64 clock_timestamp;
                            u64 steady_timestamp;
                        };
                        std::vector<GlobalEvent> global_events;
                        const int chunk_size = 1000;
                        for (s32 current = start_idx; current < end_idx; current += chunk_size) {
                            s32 to_read = std::min(chunk_size, end_idx - current);
                            std::vector<PdmPlayEvent> pdm_events(to_read);
                            s32 actual_read = 0;
                            if (R_SUCCEEDED(pdmqryQueryPlayEvent(current, pdm_events.data(), to_read, &actual_read))) {
                                for (int j = 0; j < actual_read; j++) {
                                    const auto& pe = pdm_events[j];
                                    if (pe.play_event_type == PdmPlayEventType_Applet) {
                                        u64 t1 = ((u64)pe.event_data.applet.program_id[0] << 32) | pe.event_data.applet.program_id[1];
                                        u64 t2 = ((u64)pe.event_data.applet.program_id[1] << 32) | pe.event_data.applet.program_id[0];
                                        u64 title_id = (t1 == m_entry.app_id || t2 == m_entry.app_id) ? m_entry.app_id : t1;
                                        
                                        if (title_id == m_entry.app_id) {
                                            int et = -1;
                                            switch (pe.event_data.applet.event_type) {
                                                case PdmAppletEventType_Launch: et = 0; break;
                                                case PdmAppletEventType_InFocus: et = 1; break;
                                                case PdmAppletEventType_OutOfFocus:
                                                case PdmAppletEventType_OutOfFocus4: et = 2; break;
                                                case PdmAppletEventType_Exit:
                                                case PdmAppletEventType_Exit5:
                                                case PdmAppletEventType_Exit6: et = 3; break;
                                            }
                                            if (et != -1) {
                                                GlobalEvent ge{};
                                                ge.type = et;
                                                ge.clock_timestamp = pe.timestamp_user;
                                                ge.steady_timestamp = pe.timestamp_steady;
                                                global_events.push_back(ge);
                                            }
                                        }
                                    } else if (pe.play_event_type == PdmPlayEventType_Account) {
                                        int et = -1;
                                        if (pe.event_data.account.type == 0) et = 4;
                                        else if (pe.event_data.account.type == 1) et = 5;
                                        
                                        if (et != -1) {
                                            GlobalEvent ge{};
                                            ge.type = et;
                                            ge.uid.uid[0] = ((u64)pe.event_data.account.uid[0] << 32) | pe.event_data.account.uid[1];
                                            ge.uid.uid[1] = ((u64)pe.event_data.account.uid[2] << 32) | pe.event_data.account.uid[3];
                                            ge.clock_timestamp = pe.timestamp_user;
                                            ge.steady_timestamp = pe.timestamp_steady;
                                            global_events.push_back(ge);
                                        }
                                    }
                                }
                            }
                        }

                        auto is_same_uid = [](const AccountUid& a, const AccountUid& b) {
                            return a.uid[0] == b.uid[0] && a.uid[1] == b.uid[1];
                        };

                        auto matches_target_user = [&](const AccountUid& uid) {
                            if (is_same_uid(uid, accounts[i].uid)) return true;
                            AccountUid uid_b;
                            uid_b.uid[0] = ((uid.uid[0] & 0xFFFFFFFF00000000ULL) >> 32) | ((uid.uid[0] & 0x00000000FFFFFFFFULL) << 32);
                            uid_b.uid[1] = ((uid.uid[1] & 0xFFFFFFFF00000000ULL) >> 32) | ((uid.uid[1] & 0x00000000FFFFFFFFULL) << 32);
                            return is_same_uid(uid_b, accounts[i].uid);
                        };

                        struct SessionEvent {
                            int type; // 0: launch, 1: infocus, 2: outfocus, 3: exit
                            u64 clock_timestamp;
                            u64 steady_timestamp;
                        };

                        struct PlaySession {
                            u64 start_timestamp;
                            u64 end_timestamp;
                            std::vector<SessionEvent> breakdown;
                            std::vector<AccountUid> active_users;
                        };

                        std::vector<PlaySession> sessions;
                        PlaySession current_session{};
                        bool session_active = false;
                        AccountUid active_user{};
                        bool has_active_user = false;

                        for (const auto& ge : global_events) {
                            if (ge.type == 4) { // active
                                active_user = ge.uid;
                                has_active_user = true;
                                if (session_active) {
                                    current_session.active_users.push_back(ge.uid);
                                }
                            } else if (ge.type == 5) { // inactive
                                if (has_active_user && (matches_target_user(ge.uid) || is_same_uid(active_user, ge.uid))) {
                                    has_active_user = false;
                                }
                            } else { // applet event
                                if (ge.type == 0) { // launch
                                    if (session_active) {
                                        sessions.push_back(current_session);
                                    }
                                    current_session = PlaySession{};
                                    current_session.start_timestamp = ge.clock_timestamp;
                                    current_session.end_timestamp = ge.clock_timestamp;
                                    current_session.breakdown.push_back({0, ge.clock_timestamp, ge.steady_timestamp});
                                    if (has_active_user) {
                                        current_session.active_users.push_back(active_user);
                                    }
                                    session_active = true;
                                } else if (session_active) {
                                    current_session.end_timestamp = ge.clock_timestamp;
                                    current_session.breakdown.push_back({ge.type, ge.clock_timestamp, ge.steady_timestamp});
                                    if (ge.type == 3) { // exit
                                        sessions.push_back(current_session);
                                        session_active = false;
                                    }
                                }
                            }
                        }
                        if (session_active) {
                            sessions.push_back(current_session);
                        }

                        std::vector<PlaySession> user_sessions;
                        for (const auto& s : sessions) {
                            bool user_match = false;
                            for (const auto& uid : s.active_users) {
                                if (matches_target_user(uid)) {
                                    user_match = true;
                                    break;
                                }
                            }
                            if (user_match) {
                                user_sessions.push_back(s);
                            }
                        }

                        auto format_duration = [](u64 diff_sec) -> std::string {
                            u64 hours = diff_sec / 3600;
                            u64 minutes = (diff_sec / 60) % 60;
                            u64 seconds = diff_sec % 60;
                            std::string res;
                            if (hours > 0) {
                                res += std::to_string(hours) + "h ";
                            }
                            if (minutes > 0 || hours > 0) {
                                res += std::to_string(minutes) + "m ";
                            }
                            res += std::to_string(seconds) + "s";
                            return res;
                        };

                        for (auto it = user_sessions.rbegin(); it != user_sessions.rend(); ++it) {
                            time_t start_time = (time_t)it->start_timestamp;
                            struct tm tm_start;
                            localtime_r(&start_time, &tm_start);
                            char date_buf[64];
                            strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_start);

                            u64 session_playtime = 0;
                            u64 last_in_focus_steady = 0;
                            bool in_focus = false;
                            for (const auto& b : it->breakdown) {
                                if (b.type == 1) { // InFocus
                                    last_in_focus_steady = b.steady_timestamp;
                                    in_focus = true;
                                } else if (b.type == 2 || b.type == 3) { // OutFocus or Exit
                                    if (in_focus) {
                                        if (b.steady_timestamp >= last_in_focus_steady) {
                                            session_playtime += (b.steady_timestamp - last_in_focus_steady);
                                        }
                                        in_focus = false;
                                    }
                                }
                            }

                            std::string playtime_str = format_duration(session_playtime);
                            char header_text[256];
                            snprintf(header_text, sizeof(header_text), "    %s (Total: %s)", date_buf, playtime_str.c_str());
                            m_entries.emplace_back();
                            strncpy(m_entries.back().lang.name, header_text, sizeof(m_entries.back().lang.name) - 1);
                            displayed++;

                            u64 last_ts = 0;
                            for (const auto& b : it->breakdown) {
                                time_t event_time = (time_t)b.clock_timestamp;
                                struct tm tm_evt;
                                localtime_r(&event_time, &tm_evt);
                                char time_buf[32];
                                strftime(time_buf, sizeof(time_buf), "%H:%M", &tm_evt);

                                std::string event_str;
                                bool show_duration = false;
                                u64 duration = 0;

                                if (b.type == 0) { // Launch
                                    event_str = "Application Launched";
                                    last_ts = b.steady_timestamp;
                                } else if (b.type == 1) { // InFocus
                                    event_str = "Application Resumed";
                                    last_ts = b.steady_timestamp;
                                } else if (b.type == 2) { // OutFocus
                                    event_str = "Application Suspended";
                                    show_duration = true;
                                    duration = (b.steady_timestamp >= last_ts) ? (b.steady_timestamp - last_ts) : 0;
                                } else if (b.type == 3) { // Exit
                                    event_str = "Application Closed";
                                    show_duration = true;
                                    duration = (b.steady_timestamp >= last_ts) ? (b.steady_timestamp - last_ts) : 0;
                                }

                                char event_text[256];
                                if (show_duration) {
                                    std::string dur_str = format_duration(duration);
                                    snprintf(event_text, sizeof(event_text), "      %s - %s (%s)", time_buf, event_str.c_str(), dur_str.c_str());
                                } else {
                                    snprintf(event_text, sizeof(event_text), "      %s - %s", time_buf, event_str.c_str());
                                }
                                
                                m_entries.emplace_back();
                                strncpy(m_entries.back().lang.name, event_text, sizeof(m_entries.back().lang.name) - 1);
                                displayed++;
                            }

                            if (displayed >= 200) break;
                        }
                    }

                    if (displayed == 0) {
                        // Fallback to Account Play Events (query backward to prevent UI freeze and scan efficiently)
                        s32 acc_total = 0, acc_start = 0, acc_end = 0;
                        if (R_SUCCEEDED(pdmqryGetAvailableAccountPlayEventRange(accounts[i].uid, &acc_total, &acc_start, &acc_end)) && acc_total > 0) {
                            std::vector<u64> play_times;
                            const int chunk_size = 1000;
                            s32 current = acc_end;
                            while (current > acc_start && play_times.size() < 200) {
                                s32 chunk_start = std::max(acc_start, current - chunk_size);
                                s32 to_read = current - chunk_start;
                                std::vector<PdmAccountPlayEvent> acc_events(to_read);
                                s32 actual_read = 0;
                                if (R_SUCCEEDED(pdmqryQueryAccountPlayEvent(chunk_start, accounts[i].uid, acc_events.data(), to_read, &actual_read))) {
                                    for (int j = actual_read - 1; j >= 0; j--) {
                                        const auto& pe = acc_events[j];
                                        u64 app_id_1 = ((u64)pe.application_id[0] << 32) | pe.application_id[1];
                                        u64 app_id_2 = ((u64)pe.application_id[1] << 32) | pe.application_id[0];
                                        if (app_id_1 == m_entry.app_id || app_id_2 == m_entry.app_id) {
                                            u64 ts = pe.timestamp0 ? pe.timestamp0 : pe.timestamp1;
                                            if (ts > 0) {
                                                play_times.push_back(ts);
                                                if (play_times.size() >= 200) {
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                } else {
                                    break;
                                }
                                current = chunk_start;
                            }

                            std::string last_date = "";
                            for (auto ts : play_times) {
                                time_t evt_time = (time_t)ts;
                                struct tm tm_evt;
                                localtime_r(&evt_time, &tm_evt);
                                char date_buf[64];
                                strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_evt);
                                char time_buf[32];
                                strftime(time_buf, sizeof(time_buf), "%H:%M", &tm_evt);

                                if (std::string(date_buf) != last_date) {
                                    last_date = date_buf;
                                    char header_text[256];
                                    snprintf(header_text, sizeof(header_text), "    %s", date_buf);
                                    m_entries.emplace_back();
                                    strncpy(m_entries.back().lang.name, header_text, sizeof(m_entries.back().lang.name) - 1);
                                    displayed++;
                                }

                                char event_text[256];
                                snprintf(event_text, sizeof(event_text), "      %s - Played", time_buf);
                                m_entries.emplace_back();
                                strncpy(m_entries.back().lang.name, event_text, sizeof(m_entries.back().lang.name) - 1);
                                displayed++;

                                if (displayed >= 200) break;
                            }
                        }
                    }

                    if (displayed == 0) {
                        m_entries.emplace_back();
                        strncpy(m_entries.back().lang.name, "    No play history logs found.", sizeof(m_entries.back().lang.name) - 1);
                    }
                } else {
                    // Existing summary logic
                    u64 minutes = user_playtimes[i] / 60000000000ULL;
                    u64 hours = minutes / 60;
                    minutes %= 60;
                    
                    std::string launches_str = "";
                    if (i < user_launches.size()) {
                        launches_str = " (" + std::to_string(user_launches[i]) + " plays)";
                    }

                    std::string text = "  " + user_name + ": " + std::to_string(hours) + "h " + std::to_string(minutes) + "m" + launches_str;
                    
                    m_entries.emplace_back();
                    strncpy(m_entries.back().lang.name, text.c_str(), sizeof(m_entries.back().lang.name) - 1);

                    // Helper for time formatting
                    auto format_time = [](u64 timestamp) -> std::string {
                        if (timestamp == 0) return "Unknown";
                        time_t t = (time_t)timestamp;
                        struct tm tm;
                        localtime_r(&t, &tm);
                        char buffer[64];
                        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &tm);
                        return std::string(buffer);
                    };

                    if (i < user_first.size() && user_first[i] > 0) {
                        m_entries.emplace_back();
                        std::string t = "    First: " + format_time(user_first[i]);
                        strncpy(m_entries.back().lang.name, t.c_str(), sizeof(m_entries.back().lang.name) - 1);
                    }

                    if (i < user_last.size() && user_last[i] > 0) {
                        m_entries.emplace_back();
                        std::string t = "    Last:  " + format_time(user_last[i]);
                        strncpy(m_entries.back().lang.name, t.c_str(), sizeof(m_entries.back().lang.name) - 1);
                    }
                }
            }
        }
    } else {
        m_entries.emplace_back();
        const char* text = "No profile-specific statistics found.";
        strncpy(m_entries.back().lang.name, text, sizeof(m_entries.back().lang.name) - 1);
    }
}

void GameStatsMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
    m_list->OnUpdate(controller, touch, m_index, m_entries.size(), [this](bool touch, auto i){
        if (touch && m_index == i) {
            // No action needed for clicking stats entries usually
        } else {
            m_index = i;
        }
    });
}

void GameStatsMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    // Left Side Panel - Game Summary
    {
        const float panelX = 40;
        const float panelY = 110;
        const float infoX = panelX + 20;

        // Game Icon
        gfx::drawImage(vg, panelX + (380 - 256)/2, panelY, 256, 256, m_entry.image ? m_entry.image : App::GetDefaultImage(), 12.0f);

        // Styling for labels
        auto labelColor = theme->GetColour(ThemeEntryID_TEXT_INFO);
        auto valueColor = theme->GetColour(ThemeEntryID_TEXT);

        float y = panelY + 260; // Shifted up from 280
        
        // Game Name - Wrap if too long
        gfx::drawTextBox(vg, infoX, y, 26.0f, 370.0f, theme->GetColour(ThemeEntryID_TEXT_SELECTED), m_entry.GetName());
        y += 62;

        // Developer
        gfx::drawText(vg, infoX, y, 20.0f, m_entry.GetAuthor(), nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, labelColor);
        y += 26;

        // Title ID
        {
            char id_str[32];
            snprintf(id_str, sizeof(id_str), "ID: %016llX", (unsigned long long)m_entry.app_id);
            gfx::drawText(vg, infoX, y, 16.0f, id_str, nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, labelColor);
            y += 38;
        }

        // Display version and the conventional 16-character NSO build ID.
        if (!m_display_version.empty() || !m_build_id.empty()) {
            std::string metadata;
            if (!m_display_version.empty()) {
                metadata = "Version: " + m_display_version;
            }
            if (!m_build_id.empty()) {
                if (!metadata.empty()) metadata += "   ";
                metadata += "Build ID: " + m_build_id;
            }
            gfx::drawText(vg, infoX, y - 16, 14.0f, metadata.c_str(), nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, labelColor);
        }

        // Total Playtime
        {
            u64 minutes = m_total_playtime / 60000000000ULL;
            u64 hours = minutes / 60;
            minutes %= 60;
            std::string val = std::to_string(hours) + "h " + std::to_string(minutes) + "m";
            
            gfx::drawText(vg, infoX, y, 20.0f, "Total Play Time:", nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, labelColor);
            gfx::drawText(vg, infoX + 175, y, 20.0f, val.c_str(), nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, valueColor);
            y += 28;
        }

        // Total Launches
        {
            std::string val = std::to_string(m_total_launches);
            if (m_global_launches > m_total_launches) {
                val += " (All-time: " + std::to_string(m_global_launches) + ")";
            }

            gfx::drawText(vg, infoX, y, 20.0f, "Total Launches:", nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, labelColor);
            gfx::drawText(vg, infoX + 175, y, 20.0f, val.c_str(), nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, valueColor);
            y += 28;
        }

        // Average Session
        if (m_total_launches > 0) {
            u64 total_minutes = m_total_playtime / 60000000000ULL;
            u64 avg_minutes = total_minutes / m_total_launches;
            std::string val = std::to_string(avg_minutes) + "m";
            if (avg_minutes >= 60) {
                val = std::to_string(avg_minutes / 60) + "h " + std::to_string(avg_minutes % 60) + "m";
            }

            gfx::drawText(vg, infoX, y, 20.0f, "Avg. Session:", nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, labelColor);
            gfx::drawText(vg, infoX + 175, y, 20.0f, val.c_str(), nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, valueColor);
            y += 28;
        }

        // Helper for time formatting
        auto format_time = [](u64 timestamp) -> std::string {
            time_t t = (time_t)timestamp;
            struct tm tm;
            localtime_r(&t, &tm);
            char buffer[64];
            strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &tm);
            return std::string(buffer);
        };

        // First Played
        if (m_first_played > 0) {
            gfx::drawText(vg, infoX, y, 20.0f, "First Played:", nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, labelColor);
            gfx::drawText(vg, infoX + 175, y, 20.0f, format_time(m_first_played).c_str(), nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, valueColor);
            y += 28 ;
        }

        // Last Played
        if (m_last_played > 0) {
            gfx::drawText(vg, infoX, y, 20.0f, "Last Played:", nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, labelColor);
            gfx::drawText(vg, infoX + 175, y, 20.0f, format_time(m_last_played).c_str(), nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, valueColor);
        }

        // Separator
        gfx::drawRect(vg, 450, 110, 1, 510, theme->GetColour(ThemeEntryID_LINE));
    }

    if (m_entries.empty()) {
        return;
    }

    m_list->Draw(vg, theme, m_entries.size(), [this](auto* vg, auto* theme, const auto& v, auto pos) {
        auto& e = m_entries[pos];
        const auto& [x, y, w, h] = v;

        float fontSize = 20.0f;
        auto color = theme->GetColour(ThemeEntryID_TEXT);
        const char* text = e.GetName();

        // Simple conditional styling based on content
        if (std::string(text).find("Play Time per Profile:") != std::string::npos) {
             fontSize = 24.0f;
             color = theme->GetColour(ThemeEntryID_TEXT_INFO);
        }

        gfx::drawText(vg, x, y + h/2, fontSize, text, nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, color);
    });
}

} // namespace sphaira::ui::menu::game
