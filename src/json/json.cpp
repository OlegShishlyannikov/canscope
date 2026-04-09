#include "json.hpp"
#include <ftxui/dom/table.hpp>
#include <utility>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

bool ParseJSON(const std::string &input, nlohmann::json &out) {
  class JsonParser : public nlohmann::detail::json_sax_dom_parser<nlohmann::json, void> {
  public:
    explicit JsonParser(nlohmann::json &j) : nlohmann::detail::json_sax_dom_parser<nlohmann::json, void>(j, false) {}
    static bool parse_error(std::size_t /*position*/, const std::string & /*last_token*/, const nlohmann::json::exception &ex) {
      std::cerr << std::endl;
      std::cerr << ex.what() << std::endl;
      return false;
    }
  };
  
  JsonParser parser(out);
  return nlohmann::json::sax_parse(input, &parser);
}

ftxui::Component From(const nlohmann::json &json, bool is_last, int depth, const Expander &expander) {
  if (json.is_object())
    return FromObject(Empty(), json, is_last, depth, expander);
  if (json.is_array())
    return FromArrayAny(Empty(), json, is_last, depth, expander);
  if (json.is_string())
    return FromString(json, is_last);
  if (json.is_number())
    return FromNumber(json, is_last);
  if (json.is_boolean())
    return FromBoolean(json, is_last);
  if (json.is_null())
    return FromNull(is_last);
  return Unimplemented();
}

ftxui::Component FromString(const nlohmann::json &json, bool is_last) {
  std::string value = json;
  std::string str = "\"" + value + "\"";
  return Basic(str, ftxui::Color::GreenLight, is_last);
}

ftxui::Component FromNumber(const nlohmann::json &json, bool is_last) {
  if (json.is_number_float())
    return Basic(fmt::format("{:.6g}", json.get<double>()), ftxui::Color::CyanLight, is_last);
  return Basic(json.dump(), ftxui::Color::CyanLight, is_last);
}

ftxui::Component FromBoolean(const nlohmann::json &json, bool is_last) {
  bool value = json;
  std::string str = value ? "true" : "false";
  return Basic(str, ftxui::Color::YellowLight, is_last);
}

ftxui::Component FromNull(bool is_last) { return Basic("null", ftxui::Color::RedLight, is_last); }

ftxui::Component Unimplemented() {
  return ftxui::Renderer([] { return ftxui::text("Unimplemented"); });
}

ftxui::Component Empty() {
  return ftxui::Renderer([] { return ftxui::text(""); });
}

ftxui::Component Basic(const std::string &value, ftxui::Color c, bool is_last) {
  return ftxui::Renderer([value, c, is_last](bool) {
    auto element = ftxui::paragraph(value) | color(c);
    if (!is_last)
      element = ftxui::hbox({element, ftxui::text(",")});
    return element;
  });
}

bool IsSuitableForTableView(const nlohmann::json &) {
  return false;
}

ftxui::Component Indentation(const ftxui::Component &child) {
  return ftxui::Renderer(child, [child] {
    return ftxui::hbox({
        ftxui::text("  "),
        child->Render(),
    });
  });
}

ftxui::Component FakeHorizontal(const ftxui::Component &a, const ftxui::Component &b) {
  auto c = ftxui::Container::Vertical({a, b});
  c->SetActiveChild(b);

  return ftxui::Renderer(c, [a, b] {
    return ftxui::hbox({
        a->Render(),
        b->Render(),
    });
  });
}

class ComponentExpandable : public ftxui::ComponentBase {
public:
  explicit ComponentExpandable(const Expander &expander) : expander_(expander->Child()) {}

  bool &Expanded() const { return expander_->expanded; }

  bool OnEvent(ftxui::Event event) override {
    if (event.is_mouse())
      return ftxui::ComponentBase::OnEvent(event);
    return false;
  }

  Expander expander_;
};

ftxui::Component FromObject(const ftxui::Component &prefix, const nlohmann::json &json, bool is_last, int depth, const Expander &expander) {
  class Impl : public ComponentExpandable {
  public:
    Impl(const ftxui::Component &prefix, const nlohmann::json &json, bool is_last, int depth, const Expander &expander) : ComponentExpandable(expander) {
      Expanded() = (depth <= 1);

      auto children = ftxui::Container::Vertical({});
      int size = static_cast<int>(json.size());

      for (auto &it : json.items()) {
        bool is_children_last = --size == 0;
        children->Add(Indentation(FromKeyValue(it.key(), it.value(), is_children_last, depth + 1, expander_)));
      }

      if (is_last)
        children->Add(ftxui::Renderer([] { return ftxui::text("}"); }));
      else
        children->Add(ftxui::Renderer([] { return ftxui::text("},"); }));

      auto toggle = MyToggle("{", is_last ? "{...}" : "{...},", &Expanded());
      Add(ftxui::Container::Vertical({
          FakeHorizontal(prefix, toggle),
          Maybe(children, &Expanded()),
      }));
    }
  };

  return ftxui::Make<Impl>(prefix, json, is_last, depth, expander);
}

ftxui::Component FromKeyValue(const std::string &key, const nlohmann::json &value, bool is_last, int depth, const Expander &expander) {
  std::string str = "\"" + key + "\"";
  if (value.is_object() || value.is_array()) {
    auto prefix = ftxui::Renderer([str] {
      return ftxui::hbox({
          ftxui::text(str) | color(ftxui::Color::BlueLight),
          ftxui::text(": "),
      });
    });

    if (value.is_object())
      return FromObject(prefix, value, is_last, depth, expander);
    else
      return FromArrayAny(prefix, value, is_last, depth, expander);
  }

  auto child = From(value, is_last, depth, expander);
  return ftxui::Renderer(child, [str, child] {
    return ftxui::hbox({
        ftxui::text(str) | color(ftxui::Color::BlueLight),
        ftxui::text(": "),
        child->Render(),
    });
  });
}

ftxui::Component FromArrayAny(const ftxui::Component &prefix, const nlohmann::json &json, bool is_last, int depth, const Expander &expander) {
  class Impl : public ftxui::ComponentBase {
  public:
    Impl(const ftxui::Component &prefix, const nlohmann::json &json, bool is_last, int depth, const Expander &expander) { Add(FromArray(prefix, json, is_last, depth, expander)); }
  };

  return ftxui::Make<Impl>(prefix, json, is_last, depth, expander);
}

ftxui::Component FromArray(const ftxui::Component &prefix, const nlohmann::json &json, bool is_last, int depth, const Expander &expander) {
  class Impl : public ComponentExpandable {
  public:
    Impl(ftxui::Component prefix, const nlohmann::json &json, bool is_last, int depth, const Expander &expander)
        : ComponentExpandable(expander), prefix_(std::move(prefix)), json_(json), is_last_(is_last), depth_(depth) {
      Expanded() = (depth <= 0);
      auto children = ftxui::Container::Vertical({});
      int size = static_cast<int>(json_.size());
      for (auto &it : json_.items()) {
        bool is_children_last = --size == 0;
        children->Add(Indentation(From(it.value(), is_children_last, depth + 1, expander_)));
      }

      if (is_last)
        children->Add(ftxui::Renderer([] { return ftxui::text("]"); }));
      else
        children->Add(ftxui::Renderer([] { return ftxui::text("],"); }));

      auto toggle = MyToggle("[", is_last ? "[...]" : "[...],", &Expanded());

      auto upper = ftxui::Container::Horizontal({
          FakeHorizontal(prefix_, toggle),
      });

      // Turn this array into a table.
      if (IsSuitableForTableView(json)) {
        auto expand_button = MyButton("   ", "(table view)", [this, &expander] {
          auto *parent = Parent();
          auto replacement = FromTable(prefix_, json_, is_last_, depth_, expander);
          parent->DetachAllChildren(); // Detach this.
          parent->Add(replacement);
        });

        upper = ftxui::Container::Horizontal({upper, expand_button});
      }

      Add(ftxui::Container::Vertical({
          upper,
          Maybe(children, &Expanded()),
      }));
    }

    ftxui::Component prefix_;
    const nlohmann::json &json_;
    bool is_last_;
    int depth_;
  };

  return ftxui::Make<Impl>(prefix, json, is_last, depth, expander);
}

ftxui::Component FromTable(const ftxui::Component &prefix, const nlohmann::json &json, bool is_last, int depth, const Expander &expander) {
  class Impl : public ftxui::ComponentBase {
  public:
    Impl(ftxui::Component prefix, const nlohmann::json &json, bool is_last, int depth, const Expander &expander)
        : prefix_(std::move(prefix)), json_(json), is_last_(is_last), depth_(depth) {
      std::vector<ftxui::Component> components;

      // Turn this array into a table.
      expand_button_ = MyButton("", "(array view)", [this, &expander] {
        auto *parent = Parent();
        auto replacement = FromArray(prefix_, json_, is_last_, depth_, expander);
        replacement->OnEvent(ftxui::Event::ArrowRight);
        parent->DetachAllChildren(); // Detach this.
        parent->Add(replacement);
      });

      components.push_back(expand_button_);

      std::map<std::string, int> columns_index;
      for (auto &row : json_.items()) {
        children_.emplace_back();
        auto &children_row = children_.back();
        for (auto &cell : row.value().items()) {
          // Does it require a new column?
          if (!columns_index.count(cell.key())) {
            columns_index[cell.key()] = columns_.size();
            columns_.push_back(cell.key());
          }

          // Does the current row fits in the current column?
          if ((int)children_row.size() <= columns_index[cell.key()]) {
            children_row.resize(columns_index[cell.key()] + 1);
          }

          // Fill in the data
          auto child = From(cell.value(), /*is_last=*/true, depth_ + 1, expander);
          children_row[columns_index[cell.key()]] = child;
        }
      }
      // Layout
      for (auto &rows : children_) {
        auto row = ftxui::Container::Horizontal({});
        for (auto &cell : rows) {
          if (cell)
            row->Add(cell);
        }

        components.push_back(row);
      }

      Add(ftxui::Container::Vertical(std::move(components)));
    }

    bool OnEvent(ftxui::Event event) override final { return false; }

  private:
    ftxui::Element OnRender() override {
      std::vector<std::vector<ftxui::Element>> data;
      data.push_back({ftxui::text("") | ftxui::color(ftxui::Color::GrayDark)});

      for (auto &title : columns_) {
        data.back().push_back(ftxui::text(title));
      }

      int i = 0;

      for (auto &row_children : children_) {
        std::vector<ftxui::Element> data_row;
        data_row.push_back(ftxui::text(std::to_string(i++)) | ftxui::color(ftxui::Color::GrayDark));

        for (auto &child : row_children) {
          if (child) {

            data_row.push_back(child->Render());
          } else {

            data_row.push_back(ftxui::text(""));
          }
        }

        data.push_back(std::move(data_row));
      }

      auto table = ftxui::Table(std::move(data));
      table.SelectColumns(1, -1).SeparatorVertical(ftxui::LIGHT);
      table.SelectColumns(1, -1).Border(ftxui::LIGHT);
      table.SelectRectangle(1, -1, 0, 0).SeparatorVertical(ftxui::HEAVY);
      table.SelectRectangle(1, -1, 0, 0).Border(ftxui::HEAVY);

      return ftxui::vbox({
          ftxui::hbox({
              prefix_->Render(),
              expand_button_->Render(),
          }),

          table.Render(),
      });
    }

    std::vector<std::string> columns_;
    std::vector<std::vector<ftxui::Component>> children_;

    ftxui::Component prefix_;
    ftxui::Component expand_button_;
    const nlohmann::json &json_;
    bool is_last_;
    int depth_;
  };

  return ftxui::Make<Impl>(prefix, json, is_last, depth, expander);
}

// --- Live JSON viewer: reads leaf values from shared_ptr on each render ---

static ftxui::Component LiveLeaf(std::shared_ptr<nlohmann::json> root, nlohmann::json::json_pointer ptr, bool is_last) {
  return ftxui::Renderer([root, ptr = std::move(ptr), is_last](bool focused) -> ftxui::Element {
    const auto &val = root->at(ptr);
    std::string text_str;
    ftxui::Color c;

    if (val.is_string()) {
      text_str = "\"" + val.get<std::string>() + "\"";
      c = ftxui::Color::GreenLight;
    } else if (val.is_number()) {
      text_str = val.is_number_float() ? fmt::format("{:.6g}", val.get<double>()) : val.dump();
      c = ftxui::Color::CyanLight;
    } else if (val.is_boolean()) {
      text_str = val.get<bool>() ? "true" : "false";
      c = ftxui::Color::YellowLight;
    } else if (val.is_null()) {
      text_str = "null";
      c = ftxui::Color::RedLight;
    } else {
      text_str = val.dump();
      c = ftxui::Color::White;
    }

    auto element = ftxui::paragraph(text_str) | color(c);
    if (focused)
      element = element | ftxui::inverted | ftxui::focus;
    if (!is_last)
      element = ftxui::hbox({element, ftxui::text(",")});
    return element;
  });
}

static ftxui::Component FromLiveKeyValue(std::shared_ptr<nlohmann::json> root, nlohmann::json::json_pointer ptr,
                                         const std::string &key, bool is_last, int depth, const Expander &expander);

static ftxui::Component FromLiveObject(std::shared_ptr<nlohmann::json> root, nlohmann::json::json_pointer ptr,
                                       const ftxui::Component &prefix, bool is_last, int depth, const Expander &expander) {
  class Impl : public ComponentExpandable {
  public:
    Impl(std::shared_ptr<nlohmann::json> root, nlohmann::json::json_pointer ptr,
         const ftxui::Component &prefix, bool is_last, int depth, const Expander &expander) : ComponentExpandable(expander) {
      Expanded() = (depth <= 1);

      const auto &json = root->at(ptr);
      auto children = ftxui::Container::Vertical({});
      int size = static_cast<int>(json.size());

      for (auto &it : json.items()) {
        bool is_children_last = --size == 0;
        children->Add(Indentation(FromLiveKeyValue(root, ptr / it.key(), it.key(), is_children_last, depth + 1, expander_)));
      }

      children->Add(ftxui::Renderer([is_last] { return ftxui::text(is_last ? "}" : "},"); }));

      auto toggle = MyToggle("{", is_last ? "{...}" : "{...},", &Expanded());
      Add(ftxui::Container::Vertical({
          FakeHorizontal(prefix, toggle),
          Maybe(children, &Expanded()),
      }));
    }
  };

  return ftxui::Make<Impl>(root, ptr, prefix, is_last, depth, expander);
}

static ftxui::Component FromLiveArray(std::shared_ptr<nlohmann::json> root, nlohmann::json::json_pointer ptr,
                                      const ftxui::Component &prefix, bool is_last, int depth, const Expander &expander) {
  class Impl : public ComponentExpandable {
  public:
    Impl(std::shared_ptr<nlohmann::json> root, nlohmann::json::json_pointer ptr,
         const ftxui::Component &prefix, bool is_last, int depth, const Expander &expander) : ComponentExpandable(expander) {
      Expanded() = (depth <= 0);

      const auto &json = root->at(ptr);
      auto children = ftxui::Container::Vertical({});
      int size = static_cast<int>(json.size());

      for (int i = 0; i < static_cast<int>(json.size()); ++i) {
        bool is_children_last = --size == 0;
        children->Add(Indentation(FromLive(root, ptr / i, is_children_last, depth + 1, expander_)));
      }

      children->Add(ftxui::Renderer([is_last] { return ftxui::text(is_last ? "]" : "],"); }));

      auto toggle = MyToggle("[", is_last ? "[...]" : "[...],", &Expanded());
      Add(ftxui::Container::Vertical({
          FakeHorizontal(prefix, toggle),
          Maybe(children, &Expanded()),
      }));
    }
  };

  return ftxui::Make<Impl>(root, ptr, prefix, is_last, depth, expander);
}

static ftxui::Component FromLiveKeyValue(std::shared_ptr<nlohmann::json> root, nlohmann::json::json_pointer ptr,
                                         const std::string &key, bool is_last, int depth, const Expander &expander) {
  const auto &value = root->at(ptr);
  std::string str = "\"" + key + "\"";

  if (value.is_object()) {
    auto prefix = ftxui::Renderer([str] {
      return ftxui::hbox({ftxui::text(str) | color(ftxui::Color::BlueLight), ftxui::text(": ")});
    });
    return FromLiveObject(root, ptr, prefix, is_last, depth, expander);
  }

  if (value.is_array()) {
    auto prefix = ftxui::Renderer([str] {
      return ftxui::hbox({ftxui::text(str) | color(ftxui::Color::BlueLight), ftxui::text(": ")});
    });
    return FromLiveArray(root, ptr, prefix, is_last, depth, expander);
  }

  auto child = LiveLeaf(root, ptr, is_last);
  return ftxui::Renderer(child, [str, child] {
    return ftxui::hbox({
        ftxui::text(str) | color(ftxui::Color::BlueLight),
        ftxui::text(": "),
        child->Render(),
    });
  });
}

ftxui::Component FromLive(std::shared_ptr<nlohmann::json> root, const nlohmann::json::json_pointer &ptr, bool is_last, int depth, const Expander &expander) {
  const auto &json = root->at(ptr);
  if (json.is_object())
    return FromLiveObject(root, ptr, Empty(), is_last, depth, expander);
  if (json.is_array())
    return FromLiveArray(root, ptr, Empty(), is_last, depth, expander);
  return LiveLeaf(root, ptr, is_last);
}
