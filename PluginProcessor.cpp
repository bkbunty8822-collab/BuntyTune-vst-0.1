#include <juce_audio_utils/juce_audio_utils.h>
#include "PitchCore.h"
#include "VocalChain.h"

// ---------------- BuntyTune original interface ----------------
class BuntyEditor : public juce::AudioProcessorEditor
{
public:
    BuntyEditor (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& s)
        : AudioProcessorEditor (p), apvts (s)
    {
        setSize (820, 470);
        addCombo ("key", "KEY", rows[0]);
        addCombo ("scale", "SCALE", rows[0]);
        addKnob ("speed", "RETUNE SPEED", rows[0]);
        addKnob ("natural", "NATURAL", rows[0]);
        addKnob ("mix", "MIX", rows[0]);

        addKnob ("thresh", "THRESHOLD", rows[1]);
        addKnob ("ratio", "RATIO", rows[1]);
        addKnob ("attack", "ATTACK", rows[1]);
        addKnob ("release", "RELEASE", rows[1]);
        addKnob ("makeup", "MAKEUP", rows[1]);

        addKnob ("hpf", "LOW CUT", rows[2]);
        addKnob ("presence", "PRESENCE", rows[2]);
        addKnob ("air", "AIR", rows[2]);
        addKnob ("out", "OUTPUT", rows[2]);
    }

    void paint (juce::Graphics& g) override
    {
        const auto gold = juce::Colour (0xffffc247);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff15171c), 0, 0,
                                                 juce::Colour (0xff0a0b0e), 0, (float) getHeight(), false));
        g.fillAll();
        g.setColour (gold);
        g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        g.drawText ("BuntyTune", 24, 12, 300, 40, juce::Justification::centredLeft);
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Auto Key Pitch Correction + Vocal Enhance  |  by Bunty", 24, 48, 500, 18, juce::Justification::centredLeft);

        const char* titles[3] = { "PITCH", "COMPRESSOR", "ENHANCE" };
        for (int r = 0; r < 3; ++r)
        {
            auto b = panel (r);
            g.setColour (juce::Colour (0xff1d2027));
            g.fillRoundedRectangle (b.toFloat(), 10.0f);
            g.setColour (gold.withAlpha (0.8f));
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (titles[r], b.getX() + 12, b.getY() + 6, 200, 16, juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        for (int r = 0; r < 3; ++r)
        {
            auto b = panel (r).reduced (10, 0).withTrimmedTop (24);
            const int n = (int) rows[r].size();
            const int w = b.getWidth() / std::max (1, n);
            for (int i = 0; i < n; ++i)
            {
                auto cell = b.removeFromLeft (w).reduced (4);
                if (auto* cb = dynamic_cast<juce::ComboBox*> (rows[r][(size_t) i]))
                    cb->setBounds (cell.withSizeKeepingCentre (cell.getWidth() - 10, 28));
                else rows[r][(size_t) i]->setBounds (cell);
            }
        }
    }

private:
    juce::Rectangle<int> panel (int r) const { return { 16, 78 + r * 128, getWidth() - 32, 118 }; }

    void addKnob (const juce::String& id, const juce::String& name, std::vector<juce::Component*>& row)
    {
        auto s = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
        s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        s->setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffffc247));
        s->setColour (juce::Slider::thumbColourId, juce::Colours::white);
        s->setName (name);
        addAndMakeVisible (*s);
        auto l = std::make_unique<juce::Label> ("", name);
        l->setJustificationType (juce::Justification::centred);
        l->setFont (juce::FontOptions (10.5f));
        l->setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        l->attachToComponent (s.get(), false);
        sliderAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, *s));
        row.push_back (s.get());
        labels.push_back (std::move (l));
        sliders.push_back (std::move (s));
    }

    void addCombo (const juce::String& id, const juce::String& name, std::vector<juce::Component*>& row)
    {
        auto c = std::make_unique<juce::ComboBox> (name);
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id))) c->addItemList (p->choices, 1);
        addAndMakeVisible (*c);
        auto l = std::make_unique<juce::Label> ("", name);
        l->setJustificationType (juce::Justification::centred);
        l->setFont (juce::FontOptions (10.5f));
        l->setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        l->attachToComponent (c.get(), false);
        comboAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, id, *c));
        row.push_back (c.get());
        labels.push_back (std::move (l));
        combos.push_back (std::move (c));
    }

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<juce::Component*> rows[3];
    std::vector<std::unique_ptr<juce::Slider>> sliders;
    std::vector<std::unique_ptr<juce::ComboBox>> combos;
    std::vector<std::unique_ptr<juce::Label>> labels;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAtt;
};

class BuntyTuneProcessor : public juce::AudioProcessor
{
public:
    BuntyTuneProcessor()
        : AudioProcessor (BusesProperties()
              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "PARAMS", createLayout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout l;
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key",
               StringArray { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 0));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale",
               StringArray { "Auto (detect)","Chromatic","Major","Minor","Minor Pentatonic","Bhairavi","Kafi","Khamaj","Bhairav" }, 0));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "speed", 1 }, "Retune Speed (ms)",
               NormalisableRange<float> (1.0f, 400.0f, 0.0f, 0.4f), 20.0f));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "natural", 1 }, "Natural (Vibrato Keep)",
               NormalisableRange<float> (0.0f, 1.0f), 0.5f));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
               NormalisableRange<float> (0.0f, 1.0f), 1.0f));
        auto F = [] (const char* id, const char* name, float lo, float hi, float def) {
            return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi), def); };
        l.add (F ("hpf",    "Low Cut (Hz)",        20.0f, 300.0f, 80.0f));
        l.add (F ("thresh", "Comp Threshold (dB)", -50.0f, 0.0f, -18.0f));
        l.add (F ("ratio",  "Comp Ratio",          1.0f, 12.0f, 3.0f));
        l.add (F ("attack", "Comp Attack (ms)",    0.5f, 80.0f, 8.0f));
        l.add (F ("release","Comp Release (ms)",   20.0f, 600.0f, 120.0f));
        l.add (F ("makeup", "Comp Makeup (dB)",    0.0f, 18.0f, 3.0f));
        l.add (F ("presence","Presence 3.5k (dB)", -6.0f, 8.0f, 2.0f));
        l.add (F ("air",    "Air 10k (dB)",        -6.0f, 8.0f, 2.0f));
        l.add (F ("out",    "Output Gain (dB)",    -12.0f, 12.0f, 0.0f));
        return l;
    }

    const juce::String getName() const override { return "BuntyTune"; }
    void prepareToPlay (double sr, int) override
    {
        core.prepare (sr, 2);
        chain.prepare (sr);
        setLatencySamples (core.getLatency());
    }
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        auto o = l.getMainOutputChannelSet();
        return (o == juce::AudioChannelSet::mono() || o == juce::AudioChannelSet::stereo())
               && o == l.getMainInputChannelSet();
    }

    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        core.setParams ((int) *apvts.getRawParameterValue ("key"),
                        (int) *apvts.getRawParameterValue ("scale"),
                        *apvts.getRawParameterValue ("speed"),
                        *apvts.getRawParameterValue ("mix"),
                        *apvts.getRawParameterValue ("natural"));
        core.process (buf.getArrayOfWritePointers(), buf.getNumChannels(), buf.getNumSamples());
        auto P = [this] (const char* id) { return (float) *apvts.getRawParameterValue (id); };
        chain.setParams (P ("hpf"), P ("thresh"), P ("ratio"), P ("attack"), P ("release"),
                         P ("makeup"), P ("presence"), P ("air"), P ("out"));
        chain.process (buf.getArrayOfWritePointers(), buf.getNumChannels(), buf.getNumSamples());
    }

    juce::AudioProcessorEditor* createEditor() override { return new BuntyEditor (*this, apvts); }
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& d) override
    {
        if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size)) apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }

private:
    juce::AudioProcessorValueTreeState apvts;
    PitchCorrector core;
    VocalChain chain;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BuntyTuneProcessor)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BuntyTuneProcessor(); }
