#include "preview/audio/audio_preview.hpp"

#include "preview/audio/audio_preview_model.hpp"
#include "preview/audio/audio_preview_view.hpp"
#include "preview/audio/audio_preview_view_model.hpp"
#include <algorithm>
#include <memory>
#include <string_view>

namespace files {
namespace {

bool extensionUsuallyAudio(const std::string& extension)
{
    constexpr std::string_view kAudioExtensions[] = {
        ".flac",
        ".mp3",
        ".wav",
    };
    return std::find(std::begin(kAudioExtensions), std::end(kAudioExtensions), extension) != std::end(kAudioExtensions);
}

class AudioPreviewPage : public PreviewPage {
public:
    explicit AudioPreviewPage(const FileEntry& file) : _title(file.name)
    {
        _model.load(file);
    }

    const std::string& title() const override
    {
        return _title;
    }

    void attach(lv_obj_t* parent) override
    {
        _view_model = std::make_unique<AudioPreviewViewModel>(_model);
        _view       = std::make_unique<AudioPreviewView>(*_view_model);
        _view->attach(parent);
    }

    void detach() override
    {
        if (_view) {
            _view->detach();
        }
        if (_view_model) {
            _view_model->onExit();
        }
        _view.reset();
        _view_model.reset();
    }

    void onKey(uint32_t key, FilesRouter& router) override
    {
        if (_view_model) {
            _view_model->onKey(key, router);
        }
    }

    void tick(uint32_t nowMs) override
    {
        if (_view_model) {
            _view_model->tick(nowMs);
        }
        if (_view) {
            _view->tick(nowMs);
        }
    }

private:
    std::string _title;
    AudioPreviewModel _model;
    std::unique_ptr<AudioPreviewViewModel> _view_model;
    std::unique_ptr<AudioPreviewView> _view;
};

class AudioPreviewSupport : public PreviewSupport {
public:
    const char* id() const override
    {
        return "audio";
    }

    bool supports(const FileEntry& file) const override
    {
        if (file.directory) {
            return false;
        }
        return file.kind == FileKind::Audio || extensionUsuallyAudio(file.extension);
    }

    std::unique_ptr<PreviewPage> open(const FileEntry& file) const override
    {
        return std::make_unique<AudioPreviewPage>(file);
    }
};

}  // namespace

std::unique_ptr<PreviewSupport> createAudioPreviewSupport()
{
    return std::make_unique<AudioPreviewSupport>();
}

}  // namespace files
