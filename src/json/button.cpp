#include "button.hpp"
#include <ftxui/component/captured_mouse.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <utility>

ftxui::Component MyButton(const char *prefix, const char *title, std::function<void()> on_click) {
  class Impl : public ftxui::ComponentBase {
  public:
    Impl(const char *prefix, const char *title, std::function<void()> on_click) : on_click_(std::move(on_click)), prefix_(prefix), title_(title) {}
    
    // Component implementation:
    ftxui::Element OnRender() override {
      auto style = Focused() ? (ftxui::Decorator(ftxui::inverted) | ftxui::focus) : ftxui::nothing;
      return ftxui::hbox({
          ftxui::text(prefix_),
          ftxui::text(title_) | style | ftxui::color(ftxui::Color::GrayDark) | ftxui::reflect(box_),
      });
    }
    
    bool OnEvent(ftxui::Event event) override {
      if (event.is_mouse() && box_.Contain(event.mouse().x, event.mouse().y)) {
        if (!CaptureMouse(event))
          return false;

        TakeFocus();

        if (event.mouse().button == ftxui::Mouse::Left && event.mouse().motion == ftxui::Mouse::Pressed) {
          on_click_();
          return true;
        }

        return false;
      }

      if (event == ftxui::Event::Return) {
        on_click_();
        return true;
      }

      return false;
    }

    bool Focusable() const final { return true; }

  private:
    std::function<void()> on_click_;
    const char *prefix_;
    const char *title_;
    ftxui::Box box_;
  };

  return ftxui::Make<Impl>(prefix, title, std::move(on_click));
}
