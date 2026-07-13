#include <juce_audio_utils/juce_audio_utils.h>

namespace {

constexpr double sampleRate = 44100.0;
constexpr int blockSize = 512;

/*
 * the current name of the built vst3 artefact.
 */
juce::String defaultPluginName() {
    return "VST3 Arduino Thing.vst3";
}

/*
 * the list of all root directories to search for the artefact.
 */
juce::Array<juce::File> pluginSearchRoots() {
    juce::Array<juce::File> roots;

    roots.add(juce::File::getCurrentWorkingDirectory());
    roots.add(juce::File(DEBUG_HOST_BUILD_DIR));
    roots.add(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory());

    return roots;
}

/* 
 * this method finds the built juce vst3 artefact in the given root directories.
 * if the artefact directory changes, please change the hard-coded values here.
 * it may be a good idea to add a --path flag to the cli.
 */
juce::File findPluginArtefact() {
    const auto pluginName = defaultPluginName();

    for (const auto& root : pluginSearchRoots()) {
        const juce::Array<juce::File> candidates {
            root.getChildFile(pluginName),
            root.getChildFile("VST3").getChildFile(pluginName),
            root.getChildFile("vst3arduinothing_vst3_artefacts").getChildFile("VST3").getChildFile(pluginName),
            root.getChildFile("vst3arduinothing_vst3_artefacts").getChildFile("Debug").getChildFile("VST3").getChildFile(pluginName),
            root.getChildFile("vst3arduinothing_vst3_artefacts").getChildFile("Release").getChildFile("VST3").getChildFile(pluginName),
            root.getChildFile("vst3arduinothing_vst3_artefacts").getChildFile("RelWithDebInfo").getChildFile("VST3").getChildFile(pluginName),
            root.getChildFile("vst3arduinothing_vst3_artefacts").getChildFile("MinSizeRel").getChildFile("VST3").getChildFile(pluginName),
        };

        for (const auto& candidate : candidates) {
            if (candidate.exists()) {
                return candidate;
            }
        }
    }

    return {};
}

/*
 * this method is a simple wrapper for the plugin artefact finder.
 * instead this uses a command line path
 */
juce::File pluginPathFromCommandLine(const juce::String& commandLine) {
    const auto args = juce::StringArray::fromTokens(commandLine, true);
    if (args.isEmpty()) {
        return findPluginArtefact();
    }

    return juce::File(args[0]).getFullPathName();
}

/* 
 * juce plugin window initializer
 * this exists so the plugin that is loaded can be displayed.
 */
class PluginWindow final : public juce::DocumentWindow {
public:
    explicit PluginWindow(juce::File pluginFile)
        : DocumentWindow("VST3 (Debugging...)",
                         juce::Colours::darkgrey,
                         DocumentWindow::closeButton),
          pluginFile_(std::move(pluginFile)) {
        setUsingNativeTitleBar(true);
        loadPlugin();
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    ~PluginWindow() override {
        clearContentComponent();
        plugin_.reset();
    }

    void closeButtonPressed() override {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    void loadPlugin() {
        if (! pluginFile_.exists()) {
            showError("Plugin not found:\n" + pluginFile_.getFullPathName());
            return;
        }

        juce::AudioPluginFormatManager formatManager;
        juce::addDefaultFormatsToManager(formatManager);

        juce::OwnedArray<juce::PluginDescription> typesFound;
        juce::VST3PluginFormat vst3Format;
        vst3Format.findAllTypesForFile(typesFound, pluginFile_.getFullPathName());

        if (typesFound.isEmpty()) {
            showError("No VST3 plugin types were found in:\n" + pluginFile_.getFullPathName());
            return;
        }

        juce::String error;
        plugin_ = formatManager.createPluginInstance(*typesFound[0], sampleRate, blockSize, error);

        if (plugin_ == nullptr) {
            showError("Could not create plugin instance:\n" + error);
            return;
        }

        plugin_->setPlayConfigDetails(0, 2, sampleRate, blockSize);
        plugin_->prepareToPlay(sampleRate, blockSize);

        auto editor = plugin_->hasEditor()
            ? std::unique_ptr<juce::AudioProcessorEditor>(plugin_->createEditorAndMakeActive())
            : std::make_unique<juce::GenericAudioProcessorEditor>(*plugin_);

        const auto width = juce::jmax(480, editor->getWidth());
        const auto height = juce::jmax(320, editor->getHeight());
        editor->setSize(width, height);
        setContentOwned(editor.release(), true);
        setName(plugin_->getName() + " Debug Host");
        setResizable(true, false);
    }

    void showError(const juce::String& message) {
        auto label = std::make_unique<juce::Label>();
        label->setJustificationType(juce::Justification::centred);
        label->setText(message, juce::dontSendNotification);
        label->setColour(juce::Label::textColourId, juce::Colours::white);
        setContentOwned(label.release(), true);
        setSize(640, 220);
    }

    juce::File pluginFile_;
    std::unique_ptr<juce::AudioPluginInstance> plugin_;
};

/*
 * juce host application initializer
 */
class DebugHostApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override {
        return "VST3 Debug Host";
    }

    const juce::String getApplicationVersion() override {
        return "1.0.0";
    }

    void initialise(const juce::String& commandLine) override {
        window_ = std::make_unique<PluginWindow>(pluginPathFromCommandLine(commandLine));
    }

    void shutdown() override {
        window_.reset();
    }

private:
    std::unique_ptr<PluginWindow> window_;
};

}  // namespace

START_JUCE_APPLICATION(DebugHostApplication)
