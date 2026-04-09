#include "mytoggle.hpp"
#include "ftxui/component/event.hpp"

namespace {
class MyToggleImpl : public ftxui::ComponentBase {
public:
  MyToggleImpl(const char *label_on, const char *label_off, bool *state) : label_on_(label_on), label_off_(label_off), state_(state) {}

private:
  // Component implementation.
  ftxui::Element OnRender() override {
    auto style = hovered_ ? ftxui::bold : ftxui::nothing;
    ftxui::Element my_text = *state_ ? ftxui::text(label_on_) : ftxui::text(label_off_);
    return my_text | style | reflect(box_);
  }

  bool OnEvent(ftxui::Event event) override {
    if (!event.is_mouse())
      return false;

    return OnMouseEvent(event);
  }

  bool OnMouseEvent(ftxui::Event event) {
    bool was_hovered = hovered_;
    hovered_ = box_.Contain(event.mouse().x, event.mouse().y);

    if (hovered_ && event.mouse().button == ftxui::Mouse::Left && event.mouse().motion == ftxui::Mouse::Pressed) {
      *state_ = !*state_;
      return true;
    }

    return hovered_ || was_hovered;
  }

  bool Focusable() const final { return false; }

  const char *label_on_;
  const char *label_off_;
  bool *const state_;
  bool hovered_ = false;
  ftxui::Box box_;
};
} // namespace

ftxui::Component MyToggle(const char *label_on, const char *label_off, bool *state) { return ftxui::Make<MyToggleImpl>(label_on, label_off, state); }
