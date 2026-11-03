#pragma once

#include "JuceHeader.h"
#include "matrix_router_plugin_processor.hpp"

namespace matrix_router {

/** @brief Clickable N x M crosspoint grid (rows = inputs, columns =
 * outputs), drawn procedurally in a single paint() rather than as one
 * Component per cell - the matrix can be up to 128 x 128 (16384 cells),
 * which would be a lot of child Components to lay out and paint. */
class MatrixGridComponent : public Component {
 public:
  static constexpr int cellSize = 26;
  static constexpr int numberHeaderSize = 30;
  static constexpr int axisLabelSize = 22;
  // Combined offset of the cell grid from this component's origin: an axis
  // title ("INPUTS"/"OUTPUTS") strip, then the per-channel number strip -
  // the axis titles are what make it possible to tell rows from columns at
  // a glance, rather than only where the channel-count steppers are set at
  // the top of the plugin window, away from the grid itself.
  static constexpr int headerSize = numberHeaderSize + axisLabelSize;

  explicit MatrixGridComponent(MatrixRouterAudioProcessor& processor)
      : processor_(processor) {
    refreshSize();
  }

  void refreshSize() {
    auto n = processor_.numInputs();
    auto m = processor_.numOutputs();
    setSize(headerSize + m * cellSize, headerSize + n * cellSize);
    repaint();
  }

  void paint(Graphics& g) override {
    auto n = processor_.numInputs();
    auto m = processor_.numOutputs();

    g.fillAll(Colour(0xff1a1a1a));

    // axis titles
    g.setColour(Colour(0xff64b5f6));
    g.setFont(Font(13.0f, Font::bold));
    g.drawText("OUTPUTS", headerSize, 0, m * cellSize, axisLabelSize,
              Justification::centred, false);
    {
      // Rotate -90 deg about the centre of the left-hand strip, then draw
      // "INPUTS" into a horizontal rectangle of the same centre and the
      // strip's (pre-rotation) dimensions transposed - the rotation spins
      // it in place into that strip, reading bottom-to-top.
      Graphics::ScopedSaveState save(g);
      float stripCentreX = axisLabelSize / 2.0f;
      float stripCentreY = static_cast<float>(headerSize) + n * cellSize / 2.0f;
      g.addTransform(AffineTransform::rotation(
          -MathConstants<float>::halfPi, stripCentreX, stripCentreY));
      Rectangle<int> textRect(
          static_cast<int>(stripCentreX - (n * cellSize) / 2.0f),
          static_cast<int>(stripCentreY - axisLabelSize / 2.0f),
          n * cellSize, axisLabelSize);
      g.drawText("INPUTS", textRect, Justification::centred, false);
    }

    g.setFont(11.0f);

    // column (output) number headers
    for (int o = 0; o < m; ++o) {
      auto area = Rectangle<int>(headerSize + o * cellSize, axisLabelSize,
                                 cellSize, numberHeaderSize);
      g.setColour(Colours::white.withAlpha(0.7f));
      g.drawText(String(o + 1), area, Justification::centred, false);
    }
    // row (input) number headers
    for (int i = 0; i < n; ++i) {
      auto area = Rectangle<int>(axisLabelSize, headerSize + i * cellSize,
                                 numberHeaderSize, cellSize);
      g.setColour(Colours::white.withAlpha(0.7f));
      g.drawText(String(i + 1), area, Justification::centred, false);
    }

    // cells
    for (int i = 0; i < n; ++i) {
      for (int o = 0; o < m; ++o) {
        Rectangle<int> cell(headerSize + o * cellSize,
                            headerSize + i * cellSize, cellSize, cellSize);
        bool on = processor_.crosspoint(i, o);
        g.setColour(on ? Colour(0xff4caf50) : Colour(0xff2a2a2a));
        g.fillRect(cell.reduced(1));
      }
    }

    // grid lines
    g.setColour(Colours::black);
    for (int o = 0; o <= m; ++o) {
      int x = headerSize + o * cellSize;
      g.drawVerticalLine(x, 0.0f, static_cast<float>(headerSize + n * cellSize));
    }
    for (int i = 0; i <= n; ++i) {
      int y = headerSize + i * cellSize;
      g.drawHorizontalLine(y, 0.0f, static_cast<float>(headerSize + m * cellSize));
    }
  }

  void mouseDown(const MouseEvent& e) override {
    auto n = processor_.numInputs();
    auto m = processor_.numOutputs();
    if (e.x < headerSize || e.y < headerSize) return;
    int o = (e.x - headerSize) / cellSize;
    int i = (e.y - headerSize) / cellSize;
    if (i < 0 || i >= n || o < 0 || o >= m) return;
    processor_.setCrosspoint(i, o, !processor_.crosspoint(i, o));
    repaint();
  }

 private:
  MatrixRouterAudioProcessor& processor_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MatrixGridComponent)
};

}  // namespace matrix_router
