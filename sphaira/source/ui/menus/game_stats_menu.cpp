#include "ui/menus/game_stats_menu.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "ui/nvg_util.hpp"
#include <cstring>
#include <ctime>
#include <cstdio>
#include <string>

namespace sphaira::ui::menu::game {

GameStatsMenu::GameStatsMenu(const Entry& entry) : grid::Menu{"", 0}, m_entry(entry) {
    this->SetActions(
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::DOWN, Action{"Scroll"_i18n, [](){}})
    );
    
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

    
    // Per-User Playtime
    if (!user_playtimes.empty()) {
        const char* header = "Play Time per Profile:";
        m_entries.emplace_back();
        strncpy(m_entries.back().lang.name, header, sizeof(m_entries.back().lang.name) - 1);
        
        for (size_t i = 0; i < user_playtimes.size(); i++) {
            if (user_playtimes[i] > 0) {
                u64 minutes = user_playtimes[i] / 60000000000ULL;
                u64 hours = minutes / 60;
                minutes %= 60;
                
                std::string user_name = (i < accounts.size()) ? accounts[i].nickname : "Profile " + std::to_string(i + 1);
                
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
