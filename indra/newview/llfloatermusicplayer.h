/**
 * @file llfloatermusicplayer.h
 * @brief S24 standalone music player floater - VLC-backed (media_plugin_libvlc),
 * playlist of local files and/or stream URLs, does not fight parcel audio.
 *
 * Copyright (c) 2026 Kirstenlee Cinquetti (Lee Quick)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef LL_LLFLOATERMUSICPLAYER_H
#define LL_LLFLOATERMUSICPLAYER_H

#include "llfloater.h"
#include "llhandle.h"

#include <map>

class LLScrollListCtrl;
class LLButton;
class LLCheckBoxCtrl;
class LLLineEditor;
class LLTextBox;
class LLSlider;
class LLPluginClassMedia;

// S24: VLC's own fixed 10-band layout (media_plugin_libvlc.cpp never
// exposes any other count or set of frequencies).
#define S24_MUSIC_PLAYER_EQ_BANDS 10

// S24: standalone playlist-driven music player. Playback engine is a raw
// LLPluginClassMedia (the SAME media-plugin/libvlc pipeline parcel audio
// itself can be backed by - see LLStreamingAudio_MediaPlugins,
// llviewermedia_streamingaudio.cpp, this class's own direct template),
// NOT an LLMediaCtrl - a visual texture-rendering widget is unnecessary
// for audio-only playback, and this is the codebase's own proven
// "headless" pattern already used for exactly this purpose.
class LLFloaterMusicPlayer : public LLFloater
{
public:
    LLFloaterMusicPlayer(const LLSD& key);
    ~LLFloaterMusicPlayer();

    bool postBuild() override;
    void onClose(bool app_quitting) override;

    // Registered with gIdleCallbacks in postBuild()/removed in onClose() -
    // pumps the plugin's own message queue (mMediaPlugin->idle(), same
    // call LLStreamingAudio_MediaPlugins::update() makes every frame) and
    // polls getStatus() for MEDIA_DONE to auto-advance the playlist.
    static void idle(void* user_data);

    // Used by MusicFilePicker (llfloatermusicplayer.cpp) to hand back
    // picked local file paths on the main thread.
    void addFiles(const std::vector<std::string>& paths);

private:
    void onPlayPause();
    void onStop();
    void onNext();
    void onPrev();
    void onVolumeChange();
    void onAddFileClicked();
    void onAddFolderClicked();
    void onAddUrlClicked();
    void onRemoveClicked();
    void onPlaylistDoubleClick();
    void onEqChanged();
    // S24: re-sends the current EQ settings to the plugin - needed after
    // every new track starts, not just when a slider moves, because
    // media_plugin_libvlc.cpp creates a fresh libvlc_media_player_t per
    // track (see playMedia()) and never re-attaches a previously-set
    // equalizer to it on its own; only an explicit eq_update message does.
    void sendEqualizerUpdate();

    // Static trampoline for LLDirPickerThread's callback - kept static (not
    // bound directly to `this`) so a folder dialog left open past this
    // floater's own close/destroy can never call back into a freed object;
    // the LLHandle is the only thing that has to survive, same discipline
    // MusicFilePicker::notify() already uses for the file picker above.
    static void onFolderPicked(const std::vector<std::string>& dirs, LLHandle<LLFloater> floater_handle);
    void addFolder(const std::string& dir_path);

    void playTrack(S32 index);
    // relinquish_parcel_audio: false only for the auto-advance path inside
    // playTrack() itself (which immediately re-asserts the guard for the
    // NEXT track) - every user-facing Stop keeps it true, handing parcel
    // audio back its normal behavior.
    void stopPlayback(bool relinquish_parcel_audio_guard);
    void updateTransportState();
    void updateNowPlayingLabel();
    void ensureMediaPlugin();

    // S24: the scroll list now interleaves non-playable "folder header"
    // rows among real track rows (see rebuildPlaylistView()), so a row's
    // POSITION in the list is no longer the same thing as its index in
    // mPlaylist - every place that used to read/set selection by raw index
    // now goes through a row's stable LLSD "value" instead: a track's own
    // id for a real track, or -(folderGroupId + 1) for its group's header
    // (the negative encoding keeps the two value spaces from ever
    // colliding, since real ids start at 1).
    void rebuildPlaylistView();
    S32 findTrackIndexById(S32 id) const;
    S32 getOrCreateFolderGroupId(const std::string& folder_path, const std::string& folder_label);

    void savePlaylist();
    void loadPlaylist();

    // S24: EQ enable/preamp/10 bands - same LLSD-XML-to-LL_PATH_USER_SETTINGS
    // shape as savePlaylist()/loadPlaylist() above, own file. Saved once on
    // close (onClose()), not on every slider commit - EQ changes have no
    // discrete "action" boundary the way a playlist add/remove does, so
    // saving continuously while dragging a slider would just be I/O churn
    // for no benefit; loaded once in postBuild(), right after the EQ
    // controls are wired up, before playTrack()'s own sendEqualizerUpdate()
    // ever has a reason to run.
    void saveEqSettings();
    void loadEqSettings();

    struct Track
    {
        std::string label;       // display name shown in the playlist
        std::string uri;         // file:// or http(s):// - whatever loadURI() takes directly
        std::string sourceLabel; // "Local file" / "Stream" / "Folder: X" - shown in the Source column
        std::string folderPath;  // grouping + persistence key for folder-added tracks; empty otherwise
        S32 id = 0;              // stable row identity for this session - never reused, never persisted
        S32 folderGroupId = -1;  // -1 = not part of an added folder
    };

    std::vector<Track> mPlaylist;
    S32 mCurrentTrack = -1;
    bool mIsPaused = false;

    S32 mNextTrackId = 1;
    S32 mNextFolderGroupId = 0;
    std::map<std::string, S32> mFolderPathToGroupId;
    std::map<S32, std::string> mFolderGroupLabels;

    // S24: loadURI()/start() only send async messages to the out-of-process
    // plugin - mMediaPlugin->getStatus() keeps reporting the PREVIOUS
    // track's MEDIA_DONE for one or more idle() ticks until the plugin
    // replies. Without this guard, idle()'s MEDIA_DONE check re-fires on
    // that stale status and chains straight through the rest of the
    // playlist near-instantly. Set true whenever playTrack() issues a new
    // load; cleared the first time a post-load status arrives.
    bool mAwaitingFreshStatus = false;

    LLPluginClassMedia* mMediaPlugin = nullptr;

    LLScrollListCtrl* mPlaylistCtrl = nullptr;
    LLButton* mPlayPauseBtn = nullptr;
    LLButton* mStopBtn = nullptr;
    LLButton* mNextBtn = nullptr;
    LLButton* mPrevBtn = nullptr;
    LLButton* mAddFileBtn = nullptr;
    LLButton* mAddFolderBtn = nullptr;
    LLButton* mAddUrlBtn = nullptr;
    LLButton* mRemoveBtn = nullptr;
    LLLineEditor* mUrlEntry = nullptr;
    LLSlider* mVolumeSlider = nullptr;
    LLTextBox* mNowPlayingText = nullptr;

    LLCheckBoxCtrl* mEqEnableCheck = nullptr;
    LLSlider* mEqPreampSlider = nullptr;
    LLSlider* mEqBandSliders[S24_MUSIC_PLAYER_EQ_BANDS] = {};

    class MusicFilePicker;
};

#endif // LL_LLFLOATERMUSICPLAYER_H
