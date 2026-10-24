#include "reverb_add_items_popup.hpp"
#include "binary_data.hpp"
#include "components/look_and_feel/colours.hpp"
#include "components/look_and_feel/fonts.hpp"

#include <algorithm>

using namespace ear::plugin::ui;

namespace {
constexpr int kRowHeight = 46;

void paintCheckableRow(juce::Graphics& g, int width, int height,
                       juce::Colour itemColour, const juce::String& name,
                       bool checked) {
  g.setColour(EarColours::Area01dp);
  g.fillRect(0, 0, width, height);

  auto checkArea = juce::Rectangle<int>(0, 0, 33, height).toFloat();
  auto icon = std::unique_ptr<juce::Drawable>(juce::Drawable::createFromImageData(
      checked ? binary_data::check_box_icon_svg
             : binary_data::check_box_outline_blank_icon_svg,
      checked ? binary_data::check_box_icon_svgSize
             : binary_data::check_box_outline_blank_icon_svgSize));
  if (icon)
    icon->drawWithin(g, checkArea.reduced(6.0f),
                     juce::RectanglePlacement::centred, 1.0f);

  auto textArea = juce::Rectangle<int>(33, 0, width - 33, height);
  g.setColour(itemColour);
  g.fillRect(textArea.removeFromLeft(5));
  textArea.removeFromLeft(10);
  g.setColour(EarColours::Text.withAlpha(Emphasis::high));
  g.setFont(EarFontsSingleton::instance().Items);
  g.drawText(name.isEmpty() ? juce::String("(unnamed)") : name, textArea,
            juce::Justification::centredLeft);
}
}  // namespace

// --- Objects column: functional checklist, toggled by click -------------
class ReverbAddItemsPopup::ObjectsListModel : public juce::ListBoxModel {
 public:
  explicit ObjectsListModel(ReverbAudioProcessor* p) : p_(p) {}

  void refresh() {
    items_ = p_->getAvailableObjects();
    checkedIds_.erase(
        std::remove_if(checkedIds_.begin(), checkedIds_.end(),
                       [this](std::string const& id) {
                         return std::none_of(
                             items_.begin(), items_.end(),
                             [&](auto const& o) { return o.connectionId == id; });
                       }),
        checkedIds_.end());
  }

  std::vector<std::string> const& checkedIds() const { return checkedIds_; }

  int getNumRows() override { return static_cast<int>(items_.size()); }

  void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool) override {
    if (row < 0 || row >= static_cast<int>(items_.size())) return;
    auto const& obj = items_[static_cast<size_t>(row)];
    bool checked = std::find(checkedIds_.begin(), checkedIds_.end(),
                             obj.connectionId) != checkedIds_.end();
    paintCheckableRow(g, w, h, juce::Colour(obj.colour), obj.name, checked);
  }

  void listBoxItemClicked(int row, const juce::MouseEvent&) override {
    if (row < 0 || row >= static_cast<int>(items_.size())) return;
    auto const& id = items_[static_cast<size_t>(row)].connectionId;
    auto it = std::find(checkedIds_.begin(), checkedIds_.end(), id);
    if (it != checkedIds_.end())
      checkedIds_.erase(it);
    else
      checkedIds_.push_back(id);
    if (owner) owner->repaintRow(row);
  }

  juce::ListBox* owner = nullptr;

 private:
  ReverbAudioProcessor* p_;
  std::vector<ReverbAudioProcessor::SourceObjectInfo> items_;
  std::vector<std::string> checkedIds_;
};

// --- DirectSpeakers/HOA columns: display-only, greyed out ----------------
class ReverbAddItemsPopup::UnsupportedListModel : public juce::ListBoxModel {
 public:
  using Getter = std::function<std::vector<ReverbAudioProcessor::UnsupportedInputInfo>()>;
  explicit UnsupportedListModel(Getter getter) : getter_(std::move(getter)) {}

  void refresh() { items_ = getter_(); }

  int getNumRows() override { return static_cast<int>(items_.size()); }

  void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool) override {
    if (row < 0 || row >= static_cast<int>(items_.size())) return;
    auto const& item = items_[static_cast<size_t>(row)];
    g.setColour(EarColours::Area01dp);
    g.fillRect(0, 0, w, h);
    auto textArea = juce::Rectangle<int>(10, 0, w - 10, h);
    g.setColour(juce::Colour(item.colour).withAlpha(Emphasis::disabled));
    g.fillRect(textArea.removeFromLeft(5));
    textArea.removeFromLeft(10);
    g.setColour(EarColours::Text.withAlpha(Emphasis::disabled));
    g.setFont(EarFontsSingleton::instance().Items);
    g.drawText(item.name.isEmpty() ? juce::String("(unnamed)") : item.name,
              textArea, juce::Justification::centredLeft);
  }

 private:
  Getter getter_;
  std::vector<ReverbAudioProcessor::UnsupportedInputInfo> items_;
};

ReverbAddItemsPopup::ReverbAddItemsPopup(ReverbAudioProcessor* p) : p_(p) {
  titleLabel_.setText("+ Item", juce::dontSendNotification);
  titleLabel_.setFont(juce::Font(15.0f, juce::Font::bold));
  addAndMakeVisible(titleLabel_);

  closeButton_.onClick = [this] { if (onClose) onClose(); };
  addAndMakeVisible(closeButton_);

  for (auto* label : {&objectsColumnLabel_, &directSpeakersColumnLabel_,
                      &hoaColumnLabel_}) {
    label->setFont(EarFontsSingleton::instance().Label);
    label->setColour(juce::Label::backgroundColourId,
                     EarColours::ComboBoxPopupBackground.brighter(0.05f));
    addAndMakeVisible(*label);
  }
  directSpeakersColumnLabel_.setTooltip(
      "Not selectable: this reverb needs one point position per input to "
      "weight its direction, and a DirectSpeakers item is a set of "
      "discrete speaker feeds with no single position.");
  hoaColumnLabel_.setTooltip(
      "Not selectable: this reverb needs one point position per input to "
      "weight its direction, and an HOA item is a set of ambisonic "
      "component channels with no single position.");

  objectsModel_ = std::make_unique<ObjectsListModel>(p_);
  objectsList_ = std::make_unique<juce::ListBox>("objects", objectsModel_.get());
  objectsModel_->owner = objectsList_.get();
  objectsList_->setRowHeight(kRowHeight);
  addAndMakeVisible(*objectsList_);

  directSpeakersModel_ = std::make_unique<UnsupportedListModel>(
      [this] { return p_->getAvailableDirectSpeakers(); });
  directSpeakersList_ =
      std::make_unique<juce::ListBox>("directSpeakers", directSpeakersModel_.get());
  directSpeakersList_->setRowHeight(kRowHeight);
  directSpeakersList_->setEnabled(false);
  addAndMakeVisible(*directSpeakersList_);

  hoaModel_ = std::make_unique<UnsupportedListModel>(
      [this] { return p_->getAvailableHoa(); });
  hoaList_ = std::make_unique<juce::ListBox>("hoa", hoaModel_.get());
  hoaList_->setRowHeight(kRowHeight);
  hoaList_->setEnabled(false);
  addAndMakeVisible(*hoaList_);

  addButton_.onClick = [this] {
    for (auto const& id : objectsModel_->checkedIds()) p_->addInputSlot(id);
    if (onClose) onClose();
  };
  addAndMakeVisible(addButton_);

  refresh();
}

ReverbAddItemsPopup::~ReverbAddItemsPopup() = default;

void ReverbAddItemsPopup::refresh() {
  objectsModel_->refresh();
  directSpeakersModel_->refresh();
  hoaModel_->refresh();
  objectsList_->updateContent();
  directSpeakersList_->updateContent();
  hoaList_->updateContent();
  repaint();
}

void ReverbAddItemsPopup::paint(juce::Graphics& g) {
  g.setColour(EarColours::ComboBoxPopupBackground);
  g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
  g.setColour(EarColours::Sphere);
  g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);
}

void ReverbAddItemsPopup::resized() {
  auto area = getLocalBounds().reduced(2);
  auto header = area.removeFromTop(32);
  titleLabel_.setBounds(header.removeFromLeft(header.getWidth() - 32).withX(10));
  closeButton_.setBounds(header.removeFromRight(28).reduced(2));

  auto footer = area.removeFromBottom(40);
  addButton_.setBounds(footer.reduced(8, 6));

  auto columnLabels = area.removeFromTop(24);
  int colWidth = area.getWidth() / 3;
  objectsColumnLabel_.setBounds(columnLabels.removeFromLeft(colWidth));
  directSpeakersColumnLabel_.setBounds(columnLabels.removeFromLeft(colWidth));
  hoaColumnLabel_.setBounds(columnLabels);

  objectsList_->setBounds(area.removeFromLeft(colWidth));
  directSpeakersList_->setBounds(area.removeFromLeft(colWidth));
  hoaList_->setBounds(area);
}
