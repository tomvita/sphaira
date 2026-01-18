#pragma once

#include "ui/menus/grid_menu_base.hpp"
#include "ui/menus/game_menu.hpp"

namespace sphaira::ui::menu::game {

class GameStatsMenu final : public grid::Menu {
public:
    GameStatsMenu(const Entry& entry);
    ~GameStatsMenu();

    auto GetShortTitle() const -> const char* override;
    
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

private:
    void InitEntries();
    
    const Entry m_entry;
    std::vector<Entry> m_entries{};
    std::unique_ptr<List> m_list{};
    s64 m_index{0};
    u64 m_total_playtime{0};
    u32 m_total_launches{0};
    u64 m_last_played{0};
    u64 m_first_played{0};
    u32 m_global_launches{0};
    bool m_show_full_history{false};
};

} // namespace sphaira::ui::menu::game
