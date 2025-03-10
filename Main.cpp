/*
  ==============================================================================

    BufferedRecorderSampler.h
    Created: March 2, 2025
    Author:  Claude

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
 * State enumeration for the plugin
 */
enum class PluginState
{
    Recording,
    Trimming,
    Sampling
};

//==============================================================================
/**
 * Pitch detector class using YIN algorithm
 */
class PitchDetector
{
public:
    PitchDetector(double sampleRate, int bufferSize)
        : sampleRate(sampleRate), bufferSize(bufferSize)
    {
        yinBuffer.resize(bufferSize / 2);
    }

    float detectPitch(const float* buffer, int size)
    {
        // YIN algorithm for pitch detection
        // Step 1: Calculate difference function
        for (int tau = 0; tau < yinBuffer.size(); tau++)
        {
            yinBuffer[tau] = 0.0f;
            for (int j = 0; j < yinBuffer.size(); j++)
            {
                float delta = buffer[j] - buffer[j + tau];
                yinBuffer[tau] += delta * delta;
            }
        }

        // Step 2: Cumulative mean normalized difference function
        float sum = 0.0f;
        yinBuffer[0] = 1.0f;

        for (int tau = 1; tau < yinBuffer.size(); tau++)
        {
            sum += yinBuffer[tau];
            yinBuffer[tau] *= tau / sum;
        }

        // Step 3: Find the first minimum below threshold
        int tau = 2;
        float threshold = 0.1f;

        while (tau < yinBuffer.size())
        {
            if (yinBuffer[tau] < threshold &&
                yinBuffer[tau] < yinBuffer[tau - 1] &&
                yinBuffer[tau] < yinBuffer[tau + 1])
            {
                // Refine the estimate with parabolic interpolation
                float alpha = yinBuffer[tau - 1];
                float beta = yinBuffer[tau];
                float gamma = yinBuffer[tau + 1];
                float p = 0.5f * (alpha - gamma) / (alpha - 2.0f * beta + gamma);

                // Return the frequency
                return sampleRate / (tau + p);
            }
            tau++;
        }

        // If no pitch found
        return 0.0f;
    }

    juce::String noteFromFrequency(float frequency)
    {
        if (frequency <= 0.0f)
            return "No pitch detected";

        static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

        // A4 = 440Hz = 69th midi note
        float midiNote = 12.0f * std::log2(frequency / 440.0f) + 69.0f;
        int roundedMidiNote = juce::roundToInt(midiNote);

        // Calculate octave and note
        int octave = (roundedMidiNote / 12) - 1;
        int noteIndex = roundedMidiNote % 12;

        return juce::String(noteNames[noteIndex]) + juce::String(octave);
    }

    int midiNoteFromFrequency(float frequency)
    {
        if (frequency <= 0.0f)
            return -1;

        // A4 = 440Hz = 69th midi note
        float midiNote = 12.0f * std::log2(frequency / 440.0f) + 69.0f;
        return juce::roundToInt(midiNote);
    }

private:
    double sampleRate;
    int bufferSize;
    std::vector<float> yinBuffer;
};

//==============================================================================
/**
 * Circular audio buffer to continuously record incoming audio
 */
class CircularAudioBuffer
{
public:
    CircularAudioBuffer(int numChannels, int maxLengthInSamples)
        : buffer(numChannels, maxLengthInSamples)
    {
        writePos = 0;
        size = maxLengthInSamples;
    }

    void write(const juce::AudioBuffer<float>& sourceBuffer)
    {
        const int numSamples = sourceBuffer.getNumSamples();
        const int numChannels = juce::jmin(sourceBuffer.getNumChannels(), buffer.getNumChannels());

        for (int channel = 0; channel < numChannels; ++channel)
        {
            int pos = writePos;

            for (int i = 0; i < numSamples; ++i)
            {
                buffer.setSample(channel, pos, sourceBuffer.getSample(channel, i));

                if (++pos >= size)
                    pos = 0;
            }
        }

        writePos = (writePos + numSamples) % size;
    }

    void copyTo(juce::AudioBuffer<float>& destBuffer, int startSample, int endSample)
    {
        const int numChannels = juce::jmin(destBuffer.getNumChannels(), buffer.getNumChannels());
        const int numSamples = juce::jmin(destBuffer.getNumSamples(), endSample - startSample);

        for (int channel = 0; channel < numChannels; ++channel)
        {
            int readPos = (writePos - size + startSample) % size;
            if (readPos < 0) readPos += size;

            for (int i = 0; i < numSamples; ++i)
            {
                destBuffer.setSample(channel, i, buffer.getSample(channel, readPos));

                if (++readPos >= size)
                    readPos = 0;
            }
        }
    }

    int getSize() const { return size; }
    int getWritePosition() const { return writePos; }

private:
    juce::AudioBuffer<float> buffer;
    int writePos;
    int size;
};

//==============================================================================
/**
 * Simple sampler voice that plays a single audio buffer
 */
class BufferedSamplerVoice : public juce::SynthesiserVoice
{
public:
    BufferedSamplerVoice() = default;

    bool canPlaySound(juce::SynthesiserSound* sound) override;
    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound* sound, int currentPitchWheelPosition) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void pitchWheelMoved(int newPitchWheelValue) override;
    void controllerMoved(int controllerNumber, int newControllerValue) override;
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;

private:
    double level = 0.0;
    double tailOff = 0.0;

    int rootNote = 60; // C4

    int sourceSamplePosition = 0;
    juce::AudioBuffer<float>* sampleBuffer = nullptr;

    // Need to store rate to adjust for different pitches
    double rate = 1.0;
};

//==============================================================================
/**
 * Simple sampler sound that plays an audio buffer
 */
class BufferedSamplerSound : public juce::SynthesiserSound
{
public:
    BufferedSamplerSound(juce::AudioBuffer<float>& buffer, int rootNote)
        : sampleBuffer(buffer), rootNote(rootNote) {
    }

    bool appliesToNote(int midiNoteNumber) override { return true; }
    bool appliesToChannel(int midiChannel) override { return true; }

    juce::AudioBuffer<float>& getSampleBuffer() { return sampleBuffer; }
    int getRootNote() const { return rootNote; }

private:
    juce::AudioBuffer<float> sampleBuffer;
    int rootNote;
};

//==============================================================================
/**
 * Main processor for the buffered recorder sampler plugin
 */
class BufferedRecorderSamplerProcessor : public juce::AudioProcessor
{
public:
    //==============================================================================
    BufferedRecorderSamplerProcessor();
    ~BufferedRecorderSamplerProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    void setBufferDuration(float seconds);
    void enterTrimMode();
    void enterSamplerMode();

    PluginState getState() const { return state; }

    float getStartPosition() const { return startPosition; }
    float getEndPosition() const { return endPosition; }
    void setStartPosition(float pos) { startPosition = pos; }
    void setEndPosition(float pos) { endPosition = pos; }

    void previewTrimmedSample();
    void stopPreview();

    int getMostCommonNote() const { return mostCommonNote; }
    void detectPitch();

    CircularAudioBuffer& getCircularBuffer() { return circularBuffer; }
    juce::AudioBuffer<float>& getTrimmedBuffer() { return trimmedBuffer; }

private:
    //==============================================================================
    PluginState state = PluginState::Recording;

    // Circular buffer for continuous recording
    CircularAudioBuffer circularBuffer;

    // Buffer for the trimmed sample
    juce::AudioBuffer<float> trimmedBuffer;

    // Duration of the buffer in seconds
    float bufferDuration = 60.0f;

    // Trim positions (normalized 0.0 - 1.0)
    float startPosition = 0.0f;
    float endPosition = 1.0f;

    // Pitch detection
    std::unique_ptr<PitchDetector> pitchDetector;
    std::map<int, int> noteHistogram;
    int mostCommonNote = 60; // Default to C4

    // Sampler
    juce::Synthesiser sampler;

    // Preview state
    bool isPreviewActive = false;
    int previewPosition = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BufferedRecorderSamplerProcessor)
};

//==============================================================================
/**
 * Editor component for BufferedRecorderSampler
 */
class BufferedRecorderSamplerEditor : public juce::AudioProcessorEditor,
    private juce::Button::Listener,
    private juce::Slider::Listener,
    private juce::Timer
{
public:
    BufferedRecorderSamplerEditor(BufferedRecorderSamplerProcessor&);
    ~BufferedRecorderSamplerEditor() override;

    //==============================================================================
    void paint(juce::Graphics&) override;
    void resized() override;

    void buttonClicked(juce::Button* button) override;
    void sliderValueChanged(juce::Slider* slider) override;

    void timerCallback() override;

private:
    void updateControlsVisibility();

    // Reference to the processor
    BufferedRecorderSamplerProcessor& processor;

    // UI components for recorder mode
    juce::TextButton buffer10sButton{ "10s" };
    juce::TextButton buffer30sButton{ "30s" };
    juce::TextButton buffer60sButton{ "60s" };

    // UI components for trimming mode
    juce::Slider startSlider;
    juce::Slider endSlider;
    juce::TextButton previewButton{ "Preview" };
    juce::TextButton doneButton{ "Done" };
    juce::Label pitchLabel{ {}, "Detected Pitch: " };

    // UI components for sampler mode
    juce::Label samplerInfoLabel{ {}, "Sampler Mode" };

    // Visual feedback
    juce::Path waveformPath;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BufferedRecorderSamplerEditor)
};

/**
 * Implementation of processor methods
 */
BufferedRecorderSamplerProcessor::BufferedRecorderSamplerProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    circularBuffer(2, 48000 * 60) // 60 seconds max at 48kHz
{
    trimmedBuffer.setSize(2, 48000 * 60); // Max buffer size
}

BufferedRecorderSamplerProcessor::~BufferedRecorderSamplerProcessor()
{
}

const juce::String BufferedRecorderSamplerProcessor::getName() const
{
    return "Pitch Sampler";
}

bool BufferedRecorderSamplerProcessor::acceptsMidi() const
{
    return true;
}

bool BufferedRecorderSamplerProcessor::producesMidi() const
{
    return false;
}

bool BufferedRecorderSamplerProcessor::isMidiEffect() const
{
    return false;
}

double BufferedRecorderSamplerProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int BufferedRecorderSamplerProcessor::getNumPrograms()
{
    return 1;
}

int BufferedRecorderSamplerProcessor::getCurrentProgram()
{
    return 0;
}

void BufferedRecorderSamplerProcessor::setCurrentProgram(int index)
{
}

const juce::String BufferedRecorderSamplerProcessor::getProgramName(int index)
{
    return {};
}

void BufferedRecorderSamplerProcessor::changeProgramName(int index, const juce::String& newName)
{
}

void BufferedRecorderSamplerProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Initialize pitch detector
    pitchDetector = std::make_unique<PitchDetector>(sampleRate, samplesPerBlock);

    // Initialize sampler
    sampler.setCurrentPlaybackSampleRate(sampleRate);

    // Clear voices and sounds
    sampler.clearVoices();
    sampler.clearSounds();

    // Add voices
    for (int i = 0; i < 16; ++i)
        sampler.addVoice(new BufferedSamplerVoice());
}

void BufferedRecorderSamplerProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

bool BufferedRecorderSamplerProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Support mono or stereo
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Input and output layouts must match
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void BufferedRecorderSamplerProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numInputChannels = getTotalNumInputChannels();
    const int numOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    // Clear any output channels that don't contain input data
    for (int i = numInputChannels; i < numOutputChannels; ++i)
        buffer.clear(i, 0, numSamples);

    // Always record incoming audio to circular buffer
    if (state == PluginState::Recording)
    {
        circularBuffer.write(buffer);
    }

    // Process audio based on state
    if (state == PluginState::Recording || state == PluginState::Trimming)
    {
        // If previewing in trim mode, mix the preview with the input
        if (state == PluginState::Trimming && isPreviewActive)
        {
            // Create temporary buffer for preview
            juce::AudioBuffer<float> previewBuffer(buffer.getNumChannels(), buffer.getNumSamples());
            previewBuffer.clear();

            int samplesToCopy = juce::jmin(numSamples, trimmedBuffer.getNumSamples() - previewPosition);

            if (samplesToCopy > 0)
            {
                // Copy from trimmed buffer to preview buffer
                for (int channel = 0; channel < previewBuffer.getNumChannels(); ++channel)
                {
                    previewBuffer.copyFrom(channel, 0,
                        trimmedBuffer,
                        channel,
                        previewPosition,
                        samplesToCopy);
                }

                previewPosition += samplesToCopy;

                // Loop if we reach the end
                if (previewPosition >= trimmedBuffer.getNumSamples())
                    previewPosition = 0;

                // Mix with input
                for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                {
                    buffer.addFrom(channel, 0, previewBuffer, channel, 0, samplesToCopy);
                }
            }
        }
        else
        {
            // Just pass through the input
            // Already handled by host
        }
    }
    else if (state == PluginState::Sampling)
    {
        // Process MIDI messages and render sampler output
        buffer.clear();
        sampler.renderNextBlock(buffer, midiMessages, 0, numSamples);
    }
}

juce::AudioProcessorEditor* BufferedRecorderSamplerProcessor::createEditor()
{
    return new BufferedRecorderSamplerEditor(*this);
}

bool BufferedRecorderSamplerProcessor::hasEditor() const
{
    return true;
}

void BufferedRecorderSamplerProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void BufferedRecorderSamplerProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

void BufferedRecorderSamplerProcessor::setBufferDuration(float seconds)
{
    bufferDuration = seconds;
}

void BufferedRecorderSamplerProcessor::enterTrimMode()
{
    state = PluginState::Trimming;

    // Calculate start and end samples
    int totalSamples = juce::roundToInt(bufferDuration * getSampleRate());
    int startSample = 0;
    int endSample = totalSamples;

    // Copy from circular buffer to trimmed buffer
    trimmedBuffer.clear();
    trimmedBuffer.setSize(2, endSample - startSample, false, true, true);
    circularBuffer.copyTo(trimmedBuffer, startSample, endSample);

    // Reset trim positions
    startPosition = 0.0f;
    endPosition = 1.0f;

    // Reset note histogram
    noteHistogram.clear();
}

void BufferedRecorderSamplerProcessor::enterSamplerMode()
{
    // Calculate start and end sample in samples
    int totalSamples = trimmedBuffer.getNumSamples();
    int startSample = juce::roundToInt(startPosition * totalSamples);
    int endSample = juce::roundToInt(endPosition * totalSamples);
    int lengthInSamples = endSample - startSample;

    // Create a new buffer for the trimmed portion
    juce::AudioBuffer<float> finalBuffer;
    finalBuffer.setSize(trimmedBuffer.getNumChannels(), lengthInSamples);

    // Copy the data from the trimmed buffer to the final buffer
    for (int channel = 0; channel < finalBuffer.getNumChannels(); ++channel)
    {
        finalBuffer.copyFrom(channel, 0,
            trimmedBuffer,
            channel,
            startSample,
            lengthInSamples);
    }

    // Clear existing sounds and create new sampler sound
    sampler.clearSounds();
    sampler.addSound(new BufferedSamplerSound(finalBuffer, mostCommonNote));

    // Change state to sampling
    state = PluginState::Sampling;
}

void BufferedRecorderSamplerProcessor::previewTrimmedSample()
{
    // Calculate start and end sample in samples
    int totalSamples = trimmedBuffer.getNumSamples();
    int startSample = juce::roundToInt(startPosition * totalSamples);
    int endSample = juce::roundToInt(endPosition * totalSamples);

    // Create a temporary buffer for pitch detection
    juce::AudioBuffer<float> tempBuffer;
    tempBuffer.setSize(1, endSample - startSample);

    // Copy the first channel for pitch detection
    tempBuffer.copyFrom(0, 0,
        trimmedBuffer,
        0,
        startSample,
        endSample - startSample);

    // Start the preview
    isPreviewActive = true;
    previewPosition = startSample;

    // Run pitch detection on the preview
    detectPitch();
}

void BufferedRecorderSamplerProcessor::stopPreview()
{
    isPreviewActive = false;
}

void BufferedRecorderSamplerProcessor::detectPitch()
{
    if (trimmedBuffer.getNumSamples() == 0)
        return;

    // Calculate start and end sample in samples
    int totalSamples = trimmedBuffer.getNumSamples();
    int startSample = juce::roundToInt(startPosition * totalSamples);
    int endSample = juce::roundToInt(endPosition * totalSamples);
    int lengthInSamples = endSample - startSample;

    // Analyze in chunks
    const int chunkSize = 2048;
    int numChunks = lengthInSamples / chunkSize;

    // Process each chunk
    for (int chunk = 0; chunk < numChunks; ++chunk)
    {
        // Create a temporary buffer for the chunk
        float chunkData[chunkSize];

        // Copy data from the first channel
        for (int i = 0; i < chunkSize; ++i)
        {
            int sampleIndex = startSample + chunk * chunkSize + i;
            chunkData[i] = trimmedBuffer.getSample(0, sampleIndex);
        }

        // Detect pitch for this chunk
        float frequency = pitchDetector->detectPitch(chunkData, chunkSize);

        // Convert to MIDI note
        int midiNote = pitchDetector->midiNoteFromFrequency(frequency);

        // Add to histogram if valid
        if (midiNote >= 0 && midiNote < 128)
        {
            if (noteHistogram.find(midiNote) == noteHistogram.end())
                noteHistogram[midiNote] = 1;
            else
                noteHistogram[midiNote]++;
        }
    }

    // Find most common note
    int maxCount = 0;
    for (const auto& entry : noteHistogram)
    {
        if (entry.second > maxCount)
        {
            maxCount = entry.second;
            mostCommonNote = entry.first;
        }
    }
}

/**
 * Implementation of editor methods
 */
BufferedRecorderSamplerEditor::BufferedRecorderSamplerEditor(BufferedRecorderSamplerProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    // Initialize UI components

    // Buffer duration buttons
    addAndMakeVisible(buffer10sButton);
    addAndMakeVisible(buffer30sButton);
    addAndMakeVisible(buffer60sButton);

    buffer10sButton.addListener(this);
    buffer30sButton.addListener(this);
    buffer60sButton.addListener(this);

    // Trimming controls
    addAndMakeVisible(startSlider);
    addAndMakeVisible(endSlider);
    addAndMakeVisible(previewButton);
    addAndMakeVisible(doneButton);
    addAndMakeVisible(pitchLabel);

    startSlider.setRange(0.0, 1.0);
    endSlider.setRange(0.0, 1.0);
    startSlider.setValue(0.0);
    endSlider.setValue(1.0);

    startSlider.addListener(this);
    endSlider.addListener(this);
    previewButton.addListener(this);
    doneButton.addListener(this);

    // Sampler info
    addAndMakeVisible(samplerInfoLabel);

    // Set initial sizes
    setSize(600, 400);

    // Start timer for UI updates
    startTimerHz(30);

    // Update UI based on current state
    updateControlsVisibility();
}

BufferedRecorderSamplerEditor::~BufferedRecorderSamplerEditor()
{
    stopTimer();
}

void BufferedRecorderSamplerEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    // Draw title
    g.setColour(juce::Colours::white);
    g.setFont(18.0f);
    g.drawText("Buffered Recorder/Sampler", getLocalBounds().withHeight(30), juce::Justification::centred, true);

    // Draw state info
    g.setFont(14.0f);
    juce::String stateString;

    switch (processor.getState())
    {
    case PluginState::Recording:
        stateString = "Recording Mode";
        break;
    case PluginState::Trimming:
        stateString = "Sample Trimming Mode";
        break;
    case PluginState::Sampling:
        stateString = "Sampler Mode";
        break;
    }

    g.drawText(stateString, getLocalBounds().withHeight(50).withY(30), juce::Justification::centred, true);

    // Draw waveform
    if (processor.getState() == PluginState::Trimming)
    {
        g.setColour(juce::Colours::lightblue);
        g.strokePath(waveformPath, juce::PathStrokeType(1.0f));

        // Draw trim markers
        float startX = processor.getStartPosition() * getWidth();
        float endX = processor.getEndPosition() * getWidth();

        g.setColour(juce::Colours::red);
        g.drawLine(startX, 100.0f, startX, 200.0f, 2.0f);

        g.setColour(juce::Colours::green);
        g.drawLine(endX, 100.0f, endX, 200.0f, 2.0f);
    }
}

void BufferedRecorderSamplerEditor::resized()
{
    // Position UI components
    int margin = 20;
    int buttonHeight = 30;
    int buttonWidth = 100;

    // Record mode buttons positioning
    buffer10sButton.setBounds(margin, 80, buttonWidth, buttonHeight);
    buffer30sButton.setBounds(margin * 2 + buttonWidth, 80, buttonWidth, buttonHeight);
    buffer60sButton.setBounds(margin * 3 + buttonWidth * 2, 80, buttonWidth, buttonHeight);

    // Trim controls positioning
    startSlider.setBounds(margin, 250, getWidth() - margin * 2, buttonHeight);
    endSlider.setBounds(margin, 290, getWidth() - margin * 2, buttonHeight);
    previewButton.setBounds(margin, 330, buttonWidth, buttonHeight);
    doneButton.setBounds(getWidth() - margin - buttonWidth, 330, buttonWidth, buttonHeight);
    pitchLabel.setBounds(margin * 2 + buttonWidth, 330, getWidth() - margin * 3 - buttonWidth * 2, buttonHeight);

    // Sampler info positioning
    samplerInfoLabel.setBounds(margin, 150, getWidth() - margin * 2, buttonHeight * 2);
}

void BufferedRecorderSamplerEditor::buttonClicked(juce::Button* button)
{
    if (button == &buffer10sButton)
    {
        processor.setBufferDuration(10.0f);
        processor.enterTrimMode();
    }
    else if (button == &buffer30sButton)
    {
        processor.setBufferDuration(30.0f);
        processor.enterTrimMode();
    }
    else if (button == &buffer60sButton)
    {
        processor.setBufferDuration(60.0f);
        processor.enterTrimMode();
    }
    else if (button == &previewButton)
    {
        processor.previewTrimmedSample();
    }
    else if (button == &doneButton)
    {
        processor.enterSamplerMode();
    }

    updateControlsVisibility();
}

void BufferedRecorderSamplerEditor::sliderValueChanged(juce::Slider* slider)
{
    if (slider == &startSlider)
    {
        processor.setStartPosition(static_cast<float>(slider->getValue()));

        // Ensure start is before end
        if (processor.getStartPosition() >= processor.getEndPosition())
        {
            processor.setStartPosition(processor.getEndPosition() - 0.01f);
            startSlider.setValue(processor.getStartPosition());
        }
    }
    else if (slider == &endSlider)
    {
        processor.setEndPosition(static_cast<float>(slider->getValue()));

        // Ensure end is after start
        if (processor.getEndPosition() <= processor.getStartPosition())
        {
            processor.setEndPosition(processor.getStartPosition() + 0.01f);
            endSlider.setValue(processor.getEndPosition());
        }
    }

    repaint();
}

void BufferedRecorderSamplerEditor::timerCallback()
{
    // Update state-dependent UI
    updateControlsVisibility();

    // Update waveform visualization if in trimming mode
    if (processor.getState() == PluginState::Trimming)
    {
        auto& buffer = processor.getTrimmedBuffer();

        if (buffer.getNumSamples() > 0)
        {
            // Create a path for the waveform
            waveformPath.clear();

            const float height = 100.0f;
            const float centerY = 150.0f;

            // Scale for drawing
            const float xScale = getWidth() / static_cast<float>(buffer.getNumSamples());

            // Start path at first sample
            waveformPath.startNewSubPath(0, centerY);

            // Add points for samples
            for (int i = 0; i < buffer.getNumSamples(); i += 128) // Downsample for drawing
            {
                float sample = buffer.getSample(0, i); // Just use first channel
                float y = centerY - sample * height;
                float x = i * xScale;

                waveformPath.lineTo(x, y);
            }

            // End path at last sample
            waveformPath.lineTo(getWidth(), centerY);
        }

        // Update pitch label
        juce::String pitchText = "Detected Pitch: ";

        if (processor.getMostCommonNote() >= 0)
        {
            auto noteString = juce::MidiMessage::getMidiNoteName(
                processor.getMostCommonNote(),
                true,
                true,
                3
            );

            pitchText += noteString;
        }
        else
        {
            pitchText += "None";
        }

        pitchLabel.setText(pitchText, juce::dontSendNotification);
    }

    repaint();
}

void BufferedRecorderSamplerEditor::updateControlsVisibility()
{
    // Show/hide controls based on current state
    switch (processor.getState())
    {
    case PluginState::Recording:
        buffer10sButton.setVisible(true);
        buffer30sButton.setVisible(true);
        buffer60sButton.setVisible(true);

        startSlider.setVisible(false);
        endSlider.setVisible(false);
        previewButton.setVisible(false);
        doneButton.setVisible(false);
        pitchLabel.setVisible(false);

        samplerInfoLabel.setVisible(false);
        break;

    case PluginState::Trimming:
        buffer10sButton.setVisible(false);
        buffer30sButton.setVisible(false);
        buffer60sButton.setVisible(false);

        startSlider.setVisible(true);
        endSlider.setVisible(true);
        previewButton.setVisible(true);
        doneButton.setVisible(true);
        pitchLabel.setVisible(true);

        samplerInfoLabel.setVisible(false);
        break;

    case PluginState::Sampling:
        buffer10sButton.setVisible(false);
        buffer30sButton.setVisible(false);
        buffer60sButton.setVisible(false);

        startSlider.setVisible(false);
        endSlider.setVisible(false);
        previewButton.setVisible(false);
        doneButton.setVisible(false);
        pitchLabel.setVisible(false);

        samplerInfoLabel.setVisible(true);
        samplerInfoLabel.setText("Sampler Mode Active\nRoot Note: " +
            juce::MidiMessage::getMidiNoteName(processor.getMostCommonNote(), true, true, 3),
            juce::dontSendNotification);
        break;
    }
}

/**
 * Implementation of BufferedSamplerVoice methods
 */
bool BufferedSamplerVoice::canPlaySound(juce::SynthesiserSound* sound)
{
    return dynamic_cast<BufferedSamplerSound*>(sound) != nullptr;
}

void BufferedSamplerVoice::startNote(int midiNoteNumber, float velocity,
    juce::SynthesiserSound* sound, int /*currentPitchWheelPosition*/)
{
    if (auto* samplerSound = dynamic_cast<BufferedSamplerSound*>(sound))
    {
        // Get the root note
        rootNote = samplerSound->getRootNote();

        // Calculate playback rate based on note difference
        double ratio = std::pow(2.0, (midiNoteNumber - rootNote) / 12.0);
        rate = ratio;

        // Get the buffer
        sampleBuffer = &samplerSound->getSampleBuffer();

        // Reset position
        sourceSamplePosition = 0;

        // Set level based on velocity
        level = velocity * 0.15;

        // Clear the tailoff
        tailOff = 0.0;
    }
    else
    {
        jassertfalse; // This should never happen
    }
}

void BufferedSamplerVoice::stopNote(float velocity, bool allowTailOff)
{
    if (allowTailOff)
    {
        // Start a tail-off by setting this flag
        if (tailOff == 0.0) // Only start a new tailoff if we're not already in one
            tailOff = 1.0;
    }
    else
    {
        // We're being told to stop immediately
        clearCurrentNote();
        level = 0.0;
    }
}

void BufferedSamplerVoice::pitchWheelMoved(int newPitchWheelValue)
{
    // Could implement pitch bend here
}

void BufferedSamplerVoice::controllerMoved(int controllerNumber, int newControllerValue)
{
    // Handle MIDI controllers here
}

void BufferedSamplerVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
    int startSample,
    int numSamples)
{
    if (sampleBuffer == nullptr)
        return;

    const float* const inL = sampleBuffer->getReadPointer(0);
    const float* const inR = sampleBuffer->getNumChannels() > 1 ? sampleBuffer->getReadPointer(1) : nullptr;

    float* outL = outputBuffer.getWritePointer(0, startSample);
    float* outR = outputBuffer.getNumChannels() > 1 ? outputBuffer.getWritePointer(1, startSample) : nullptr;

    const int bufferSize = sampleBuffer->getNumSamples();

    if (bufferSize <= 0)
        return;

    while (--numSamples >= 0)
    {
        // Get the current sample position
        const int pos = static_cast<int>(sourceSamplePosition);

        // Check if we've reached the end
        if (pos >= bufferSize)
        {
            clearCurrentNote();
            break;
        }

        // Simple linear interpolation
        const float alpha = static_cast<float>(sourceSamplePosition - pos);
        const float invAlpha = 1.0f - alpha;

        // Next sample (for interpolation)
        const int nextPos = pos + 1 < bufferSize ? pos + 1 : pos;

        // Get the interpolated sample values
        float l = (inL[pos] * invAlpha + inL[nextPos] * alpha);
        float r = inR != nullptr ? (inR[pos] * invAlpha + inR[nextPos] * alpha) : l;

        // Apply level/envelope
        float currentLevel = level;

        if (tailOff > 0.0)
        {
            // Apply tailoff
            currentLevel *= static_cast<float>(tailOff);

            // Reduce the tailoff level
            tailOff *= 0.99;

            // Check if we're done with the tailoff
            if (tailOff <= 0.005)
            {
                clearCurrentNote();
                break;
            }
        }

        // Add to output buffer
        *outL++ += l * currentLevel;
        if (outR != nullptr)
            *outR++ += r * currentLevel;

        // Increment position
        sourceSamplePosition += rate;
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BufferedRecorderSamplerProcessor();
}