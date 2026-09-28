/**
 * @file llfloatermusicplayer.cpp
 * @brief S24 standalone music player floater implementation.
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

#include "llviewerprecompiledheaders.h"

#include "llfloatermusicplayer.h"

#include <filesystem>

#include "llbutton.h"
#include "llcallbacklist.h"
#include "llcheckboxctrl.h"
#include "lldir.h"
#include "lldirpicker.h"
#include "lllineeditor.h"
#include "llpluginclassmedia.h"
#include "llpluginclassmediaowner.h"
#include "llscrolllistctrl.h"
#include "llsdserialize.h"
#include "llslider.h"
#include "lltextbox.h"
#include "llviewermedia.h"
#include "llviewermenufile.h" // LLFilePickerThread
#include "llvieweraudio.h"

namespace
{
    // S24: global to the viewer install, not per-SL-account - a local music
    // library isn't tied to which avatar you're logged in as, and this way
    // the playlist is even there to browse before login.
    std::string musicPlayerPlaylistPath()
    {
        return gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "music_player_playlist.xml");
    }
}

namespace
{
    // S24: no MP3-specific extension list exists anywhere else in this
    // codebase (LLFilePicker's own filters are by dialog-type, not a
    // reusable extension set) - kept minimal and audio-only, matching what
    // media_plugin_libvlc.cpp is actually exercised against.
    bool isAudioFile(const std::filesystem::path& path)
    {
        static const char* const kAudioExts[] =
        {
            ".mp3", ".ogg", ".oga", ".wav", ".flac", ".m4a", ".aac", ".wma", ".opus"
        };
        std::string ext = path.extension().string();
        LLStringUtil::toLower(ext);
        for (const char* candidate : kAudioExts)
        {
            if (ext == candidate)
            {
                return true;
            }
        }
        return false;
    }
}

// S24: nested picker for "add a local file" - mirrors LLThumbnailImagePicker
// (llfloaterchangeitemthumbnail.cpp), the established pattern for this
// exact class in this codebase: allocate on the heap, kick off with
// getFile(), receive the result via notify() (marshaled to the main
// thread by LLFilePickerThread's own base class - safe to touch UI
// directly there), and hold only a weak LLHandle<LLFloater> so a closed
// floater is never touched after the fact.
class LLFloaterMusicPlayer::MusicFilePicker : public LLFilePickerThread
{
public:
    MusicFilePicker(LLHandle<LLFloater> floater_handle)
        : LLFilePickerThread(LLFilePicker::FFLOAD_ALL, true)
        , mFloaterHandle(floater_handle)
    {
    }

    void notify(const std::vector<std::string>& filenames) override
    {
        if (filenames.empty())
        {
            return;
        }
        if (LLFloater* floater = mFloaterHandle.get())
        {
            static_cast<LLFloaterMusicPlayer*>(floater)->addFiles(filenames);
        }
    }

private:
    LLHandle<LLFloater> mFloaterHandle;
};

LLFloaterMusicPlayer::LLFloaterMusicPlayer(const LLSD& key)
    : LLFloater(key)
{
}

LLFloaterMusicPlayer::~LLFloaterMusicPlayer()
{
    // S24: stopPlayback() already runs from onClose() (LLFloater's own
    // close path always runs before real destruction - see LLMortician
    // discipline noted elsewhere in this codebase), but a defensive
    // second cleanup here costs nothing and guards against any path that
    // destroys this floater without onClose() having run first.
    if (mMediaPlugin)
    {
        mMediaPlugin->stop();
        delete mMediaPlugin;
        mMediaPlugin = nullptr;
    }
}

bool LLFloaterMusicPlayer::postBuild()
{
    mPlaylistCtrl = getChild<LLScrollListCtrl>("playlist_list");
    mPlaylistCtrl->setDoubleClickCallback(boost::bind(&LLFloaterMusicPlayer::onPlaylistDoubleClick, this));

    mPlayPauseBtn = getChild<LLButton>("play_pause_btn");
    mPlayPauseBtn->setClickedCallback(boost::bind(&LLFloaterMusicPlayer::onPlayPause, this));

    mStopBtn = getChild<LLButton>("stop_btn");
    mStopBtn->setClickedCallback(boost::bind(&LLFloaterMusicPlayer::onStop, this));

    mPrevBtn = getChild<LLButton>("prev_btn");
    mPrevBtn->setClickedCallback(boost::bind(&LLFloaterMusicPlayer::onPrev, this));

    mNextBtn = getChild<LLButton>("next_btn");
    mNextBtn->setClickedCallback(boost::bind(&LLFloaterMusicPlayer::onNext, this));

    mAddFileBtn = getChild<LLButton>("add_file_btn");
    mAddFileBtn->setClickedCallback(boost::bind(&LLFloaterMusicPlayer::onAddFileClicked, this));

    mAddFolderBtn = getChild<LLButton>("add_folder_btn");
    mAddFolderBtn->setClickedCallback(boost::bind(&LLFloaterMusicPlayer::onAddFolderClicked, this));

    mAddUrlBtn = getChild<LLButton>("add_url_btn");
    mAddUrlBtn->setClickedCallback(boost::bind(&LLFloaterMusicPlayer::onAddUrlClicked, this));

    mRemoveBtn = getChild<LLButton>("remove_btn");
    mRemoveBtn->setClickedCallback(boost::bind(&LLFloaterMusicPlayer::onRemoveClicked, this));

    mUrlEntry = getChild<LLLineEditor>("url_entry");

    mVolumeSlider = getChild<LLSlider>("volume_slider");
    mVolumeSlider->setCommitCallback(boost::bind(&LLFloaterMusicPlayer::onVolumeChange, this));

    mNowPlayingText = getChild<LLTextBox>("now_playing_text");

    mEqEnableCheck = getChild<LLCheckBoxCtrl>("eq_enable_check");
    mEqEnableCheck->setCommitCallback(boost::bind(&LLFloaterMusicPlayer::onEqChanged, this));

    mEqPreampSlider = getChild<LLSlider>("eq_preamp_slider");
    mEqPreampSlider->setCommitCallback(boost::bind(&LLFloaterMusicPlayer::onEqChanged, this));

    for (S32 i = 0; i < S24_MUSIC_PLAYER_EQ_BANDS; ++i)
    {
        mEqBandSliders[i] = getChild<LLSlider>(llformat("eq_band_%d", i));
        mEqBandSliders[i]->setCommitCallback(boost::bind(&LLFloaterMusicPlayer::onEqChanged, this));
    }

    loadPlaylist();
    rebuildPlaylistView();

    gIdleCallbacks.addFunction(idle, this);

    updateTransportState();
    updateNowPlayingLabel();

    return LLFloater::postBuild();
}

void LLFloaterMusicPlayer::onClose(bool app_quitting)
{
    gIdleCallbacks.deleteFunction(idle, this);
    stopPlayback(true);
    savePlaylist();
}

void LLFloaterMusicPlayer::ensureMediaPlugin()
{
    if (mMediaPlugin)
    {
        return;
    }

    // S24: same construction this codebase's own headless streaming-audio
    // client uses (LLStreamingAudio_MediaPlugins::initializeMedia(),
    // llviewermedia_streamingaudio.cpp) - default_width/height=1 (audio-
    // only, the video callbacks this plugin always wires up simply never
    // fire for content with no video track - confirmed directly in
    // media_plugin_libvlc.cpp), no owner (this floater polls getStatus()
    // itself from idle() rather than implementing LLPluginClassMediaOwner).
    mMediaPlugin = LLViewerMediaImpl::newSourceFromMediaType("audio/mpeg", nullptr, 1, 1, 1.0);
    if (mMediaPlugin)
    {
        mMediaPlugin->setLoop(false); // this floater drives track-advance itself
        mMediaPlugin->setVolume(mVolumeSlider ? mVolumeSlider->getValueF32() : 1.f);
        // S24: eq_update (sendEqualizerUpdate(), called after every track
        // starts) is a no-op plugin-side unless the equalizer object
        // already exists - eq_init creates it once, up front, on this
        // fresh plugin instance.
        mMediaPlugin->initEqualizer();
    }
}

void LLFloaterMusicPlayer::playTrack(S32 index)
{
    if (index < 0 || index >= (S32)mPlaylist.size())
    {
        return;
    }

    ensureMediaPlugin();
    if (!mMediaPlugin)
    {
        return;
    }

    // S24: claim the parcel-audio guard for as long as THIS floater owns
    // playback (set once here, cleared only by stopPlayback(true) - never
    // toggled per-track, see that function's own comment) - stopping
    // parcel audio's OWN current stream is a one-time courtesy for
    // whatever was already playing; the guard is what actually keeps it
    // from restarting on a later parcel crossing while we're still active.
    LLViewerAudio::getInstance()->setExternalPlayerActive(true);
    LLViewerAudio::getInstance()->stopInternetStreamWithAutoFade();

    mCurrentTrack = index;
    mIsPaused = false;
    mAwaitingFreshStatus = true;
    mMediaPlugin->loadURI(mPlaylist[index].uri);
    mMediaPlugin->start();

    // S24: media_plugin_libvlc.cpp creates a fresh libvlc_media_player_t
    // for every track (playMedia()) and never re-attaches a previously-set
    // equalizer to it on its own - only an explicit eq_update message does.
    // Re-sending here is what keeps the EQ actually applying across track
    // changes instead of silently going flat on every Next/Prev.
    sendEqualizerUpdate();

    // S24: select by the track's stable id, not row position - folder
    // header rows mean a row's index in the scroll list no longer matches
    // its index in mPlaylist (see rebuildPlaylistView()).
    mPlaylistCtrl->setSelectedByValue(LLSD(mPlaylist[index].id), true);
    updateTransportState();
    updateNowPlayingLabel();
}

void LLFloaterMusicPlayer::stopPlayback(bool relinquish_parcel_audio_guard)
{
    if (mMediaPlugin)
    {
        mMediaPlugin->stop();
    }
    mCurrentTrack = -1;
    mIsPaused = false;

    if (relinquish_parcel_audio_guard)
    {
        LLViewerAudio::getInstance()->setExternalPlayerActive(false);
    }

    updateTransportState();
    updateNowPlayingLabel();
}

void LLFloaterMusicPlayer::onPlayPause()
{
    if (!mMediaPlugin || mCurrentTrack < 0)
    {
        // Nothing loaded yet - play whatever's selected (a folder header
        // plays its first track), or the first track if nothing is.
        S32 idx = -1;
        LLSD selected = mPlaylistCtrl->getSelectedValue();
        if (!selected.isUndefined())
        {
            S32 raw = selected.asInteger();
            if (raw < 0)
            {
                S32 group_id = -raw - 1;
                for (S32 i = 0; i < (S32)mPlaylist.size(); ++i)
                {
                    if (mPlaylist[i].folderGroupId == group_id)
                    {
                        idx = i;
                        break;
                    }
                }
            }
            else
            {
                idx = findTrackIndexById(raw);
            }
        }
        if (idx < 0 && !mPlaylist.empty())
        {
            idx = 0;
        }
        if (idx >= 0)
        {
            playTrack(idx);
        }
        return;
    }

    if (mIsPaused)
    {
        mMediaPlugin->start();
        mIsPaused = false;
    }
    else
    {
        mMediaPlugin->pause();
        mIsPaused = true;
    }
    updateTransportState();
}

void LLFloaterMusicPlayer::onStop()
{
    stopPlayback(true);
}

void LLFloaterMusicPlayer::onNext()
{
    if (mPlaylist.empty())
    {
        return;
    }
    S32 next = (mCurrentTrack < 0) ? 0 : (mCurrentTrack + 1) % (S32)mPlaylist.size();
    playTrack(next);
}

void LLFloaterMusicPlayer::onPrev()
{
    if (mPlaylist.empty())
    {
        return;
    }
    S32 prev = (mCurrentTrack <= 0) ? (S32)mPlaylist.size() - 1 : mCurrentTrack - 1;
    playTrack(prev);
}

void LLFloaterMusicPlayer::onVolumeChange()
{
    if (mMediaPlugin)
    {
        mMediaPlugin->setVolume(mVolumeSlider->getValueF32());
    }
}

void LLFloaterMusicPlayer::onEqChanged()
{
    sendEqualizerUpdate();
}

void LLFloaterMusicPlayer::sendEqualizerUpdate()
{
    if (!mMediaPlugin || !mEqEnableCheck || !mEqPreampSlider)
    {
        return;
    }

    F32 bands[S24_MUSIC_PLAYER_EQ_BANDS];
    for (S32 i = 0; i < S24_MUSIC_PLAYER_EQ_BANDS; ++i)
    {
        bands[i] = mEqBandSliders[i] ? mEqBandSliders[i]->getValueF32() : 0.f;
    }

    mMediaPlugin->updateEqualizer(mEqEnableCheck->get(), mEqPreampSlider->getValueF32(), bands);
}

void LLFloaterMusicPlayer::onAddFileClicked()
{
    (new MusicFilePicker(getHandle()))->getFile();
}

void LLFloaterMusicPlayer::addFiles(const std::vector<std::string>& paths)
{
    bool added = false;
    for (const std::string& path : paths)
    {
        if (path.empty())
        {
            continue;
        }

        Track track;
        track.uri = "file:///" + path;
        // S24: display just the filename, not the full path - matches
        // how every other file-driven list in this UI (inventory,
        // texture picker, etc) presents local files.
        size_t slash = path.find_last_of("/\\");
        track.label = (slash == std::string::npos) ? path : path.substr(slash + 1);
        track.sourceLabel = "Local file";
        track.id = mNextTrackId++;

        mPlaylist.push_back(track);
        added = true;
    }

    if (added)
    {
        rebuildPlaylistView();
        savePlaylist();
    }
}

// static
void LLFloaterMusicPlayer::onFolderPicked(const std::vector<std::string>& dirs, LLHandle<LLFloater> floater_handle)
{
    if (dirs.empty() || dirs[0].empty())
    {
        return;
    }
    if (LLFloater* floater = floater_handle.get())
    {
        static_cast<LLFloaterMusicPlayer*>(floater)->addFolder(dirs[0]);
    }
}

void LLFloaterMusicPlayer::onAddFolderClicked()
{
    (new LLDirPickerThread(boost::bind(&LLFloaterMusicPlayer::onFolderPicked, _1, getHandle()), std::string()))->getFile();
}

void LLFloaterMusicPlayer::addFolder(const std::string& dir_path)
{
    // S24: dir_path arrived from LLDirPicker already UTF-8 (it does its own
    // ll_convert_wide_to_string() on the raw IFileDialog result - see
    // lldirpicker.cpp) - constructing std::filesystem::path from a narrow
    // std::string reinterprets those bytes through the Windows ANSI
    // codepage, not UTF-8, silently mangling any non-ASCII folder/artist
    // name. Round-tripping back through wide via this codebase's own
    // ll_convert_string_to_wide() avoids that mismatch entirely and keeps
    // every filesystem::path below built from real UTF-16, unambiguous
    // regardless of codepage.
    std::filesystem::path dir(ll_convert_string_to_wide(dir_path));

    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec) || ec)
    {
        return;
    }

    // S24: folder-add is explicitly meant to be represented as a folder,
    // not silently flattened into a pile of "Local file" rows indistinguish-
    // able from single Add File picks - every track pulled from this
    // directory is grouped under one header row (rebuildPlaylistView()),
    // so the whole folder can be selected and removed as a unit, same as
    // Remove already does for a single track.
    std::wstring wfolder_label = dir.filename().wstring();
    if (wfolder_label.empty())
    {
        // A path with a trailing slash makes filename() return empty -
        // fall back one level up for a usable display name.
        wfolder_label = dir.parent_path().filename().wstring();
    }
    std::string folder_label = ll_convert_wide_to_string(wfolder_label);

    // S24: collect matches first, then append to mPlaylist only if we
    // actually found something - an empty/no-match folder gets no header
    // and no group id burned on it.
    std::vector<Track> found;

    // S24: the (path, error_code) constructor above only makes the INITIAL
    // open non-throwing - operator++ inside this range-for still throws
    // std::filesystem::filesystem_error on a mid-scan failure (a locked
    // file, a OneDrive/cloud-sync placeholder, a permission-denied entry).
    // That's an uncaught C++ exception straight out of a button-click
    // callback - one bad entry now just ends the scan instead of taking
    // the whole viewer down.
    try
    {
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
        {
            if (ec)
            {
                break;
            }
            if (!entry.is_regular_file() || !isAudioFile(entry.path()))
            {
                continue;
            }

            // S24: path::string()/u8string() on a path built from real
            // non-ASCII text (unlike the plain-ASCII extension check above)
            // is the OTHER half of this same encoding problem - MSVC's
            // narrow conversion is locale-dependent and throws
            // std::system_error (not filesystem_error - NOT caught by the
            // catch below) for any character that can't round-trip through
            // the current codepage. Confirmed the real cause of a folder-add
            // CTD on a real music library (non-ASCII artist/track names are
            // common). Staying in wide/UTF-16 via .wstring() and converting
            // with this codebase's own ll_convert_wide_to_string() (never
            // locale-dependent, never throws) avoids it entirely.
            Track track;
            track.uri = "file:///" + ll_convert_wide_to_string(entry.path().wstring());
            track.label = ll_convert_wide_to_string(entry.path().filename().wstring());
            found.push_back(track);
        }
    }
    catch (const std::exception&)
    {
        // S24: catches std::filesystem::filesystem_error (a mid-scan
        // enumeration failure) and any stray std::system_error (an
        // encoding conversion that still couldn't round-trip) alike - the
        // whole point of this guard is "one bad entry ends the scan",
        // regardless of which of the two throws it.
        LL_WARNS() << "Music player: folder scan aborted on an unreadable entry in " << dir_path << LL_ENDL;
    }

    if (found.empty())
    {
        return;
    }

    S32 group_id = getOrCreateFolderGroupId(dir_path, folder_label);
    std::string source_label = "Folder: " + folder_label;
    for (Track& track : found)
    {
        track.id = mNextTrackId++;
        track.folderGroupId = group_id;
        track.folderPath = dir_path;
        track.sourceLabel = source_label;
        mPlaylist.push_back(track);
    }

    rebuildPlaylistView();
    savePlaylist();
}

void LLFloaterMusicPlayer::onAddUrlClicked()
{
    if (!mUrlEntry)
    {
        return;
    }

    std::string url = mUrlEntry->getText();
    LLStringUtil::trim(url);
    if (url.empty())
    {
        return;
    }

    Track track;
    track.uri = url;
    track.label = url;
    track.sourceLabel = "Stream";
    track.id = mNextTrackId++;
    mPlaylist.push_back(track);

    mUrlEntry->clear();

    rebuildPlaylistView();
    savePlaylist();
}

void LLFloaterMusicPlayer::onRemoveClicked()
{
    LLSD selected = mPlaylistCtrl->getSelectedValue();
    if (selected.isUndefined())
    {
        return;
    }
    S32 raw = selected.asInteger();

    if (raw < 0)
    {
        // S24: a folder header was selected - remove every track in that
        // group as one unit, exactly what the header row exists to let
        // you do (rather than only ever being able to prune one track at
        // a time out of a folder you added as a batch).
        S32 group_id = -raw - 1;
        bool removed_current = false;
        std::vector<Track> kept;
        kept.reserve(mPlaylist.size());
        S32 new_current = -1;
        for (S32 i = 0; i < (S32)mPlaylist.size(); ++i)
        {
            if (mPlaylist[i].folderGroupId == group_id)
            {
                if (i == mCurrentTrack)
                {
                    removed_current = true;
                }
                continue;
            }
            if (i == mCurrentTrack)
            {
                new_current = (S32)kept.size();
            }
            kept.push_back(mPlaylist[i]);
        }
        mPlaylist.swap(kept);

        if (removed_current)
        {
            stopPlayback(true);
        }
        else
        {
            mCurrentTrack = new_current;
        }
    }
    else
    {
        S32 idx = findTrackIndexById(raw);
        if (idx < 0)
        {
            return;
        }

        if (idx == mCurrentTrack)
        {
            stopPlayback(true);
        }
        else if (idx < mCurrentTrack)
        {
            --mCurrentTrack;
        }

        mPlaylist.erase(mPlaylist.begin() + idx);
    }

    rebuildPlaylistView();
    savePlaylist();
}

void LLFloaterMusicPlayer::onPlaylistDoubleClick()
{
    LLSD selected = mPlaylistCtrl->getSelectedValue();
    if (selected.isUndefined())
    {
        return;
    }
    S32 raw = selected.asInteger();

    if (raw < 0)
    {
        // Folder header double-clicked - play its first track.
        S32 group_id = -raw - 1;
        for (S32 i = 0; i < (S32)mPlaylist.size(); ++i)
        {
            if (mPlaylist[i].folderGroupId == group_id)
            {
                playTrack(i);
                return;
            }
        }
        return;
    }

    S32 idx = findTrackIndexById(raw);
    if (idx >= 0)
    {
        playTrack(idx);
    }
}

S32 LLFloaterMusicPlayer::findTrackIndexById(S32 id) const
{
    for (S32 i = 0; i < (S32)mPlaylist.size(); ++i)
    {
        if (mPlaylist[i].id == id)
        {
            return i;
        }
    }
    return -1;
}

S32 LLFloaterMusicPlayer::getOrCreateFolderGroupId(const std::string& folder_path, const std::string& folder_label)
{
    std::map<std::string, S32>::const_iterator it = mFolderPathToGroupId.find(folder_path);
    if (it != mFolderPathToGroupId.end())
    {
        return it->second;
    }

    S32 group_id = mNextFolderGroupId++;
    mFolderPathToGroupId[folder_path] = group_id;
    mFolderGroupLabels[group_id] = folder_label;
    return group_id;
}

void LLFloaterMusicPlayer::rebuildPlaylistView()
{
    if (!mPlaylistCtrl)
    {
        return;
    }

    mPlaylistCtrl->clearRows();

    S32 last_group = -2; // sentinel - never matches a real folderGroupId (-1 or >=0)
    for (S32 i = 0; i < (S32)mPlaylist.size(); ++i)
    {
        const Track& track = mPlaylist[i];

        if (track.folderGroupId >= 0 && track.folderGroupId != last_group)
        {
            // S24: entering a new folder group - insert its header row.
            // The header's own "value" is a negative encoding of the group
            // id (real track ids start at 1, so the two spaces can never
            // collide) - see the header's own comment on this scheme.
            S32 count = 0;
            for (S32 j = i; j < (S32)mPlaylist.size() && mPlaylist[j].folderGroupId == track.folderGroupId; ++j)
            {
                ++count;
            }

            std::string label = "Folder: ";
            std::map<S32, std::string>::const_iterator label_it = mFolderGroupLabels.find(track.folderGroupId);
            label += (label_it != mFolderGroupLabels.end()) ? label_it->second : std::string("?");
            label += llformat(" (%d track%s)", count, count == 1 ? "" : "s");

            LLSD header;
            header["value"] = -(track.folderGroupId + 1);
            header["columns"][0]["column"] = "track";
            header["columns"][0]["value"] = label;
            header["columns"][0]["font"]["style"] = "BOLD";
            header["columns"][1]["column"] = "source";
            header["columns"][1]["value"] = "";
            mPlaylistCtrl->addElement(header);
        }
        last_group = track.folderGroupId;

        LLSD row;
        row["value"] = track.id;
        row["columns"][0]["column"] = "track";
        row["columns"][0]["value"] = (track.folderGroupId >= 0 ? "    " : "") + track.label;
        row["columns"][1]["column"] = "source";
        row["columns"][1]["value"] = track.sourceLabel;
        mPlaylistCtrl->addElement(row);
    }

    if (mCurrentTrack >= 0 && mCurrentTrack < (S32)mPlaylist.size())
    {
        mPlaylistCtrl->setSelectedByValue(LLSD(mPlaylist[mCurrentTrack].id), true);
    }
}

void LLFloaterMusicPlayer::savePlaylist()
{
    LLSD root = LLSD::emptyArray();
    for (const Track& track : mPlaylist)
    {
        LLSD entry;
        entry["uri"] = track.uri;
        entry["label"] = track.label;
        entry["source_label"] = track.sourceLabel;
        entry["folder_path"] = track.folderPath;
        root.append(entry);
    }

    std::string path = musicPlayerPlaylistPath();
    llofstream out(path.c_str());
    if (!out.good())
    {
        LL_WARNS() << "Music player: could not save playlist to " << path << LL_ENDL;
        return;
    }
    LLSDSerialize::toXML(root, out);
}

void LLFloaterMusicPlayer::loadPlaylist()
{
    std::string path = musicPlayerPlaylistPath();
    llifstream in(path.c_str());
    if (!in.is_open())
    {
        return;
    }

    LLSD root;
    LLSDSerialize::fromXML(root, in);
    if (!root.isArray())
    {
        return;
    }

    for (LLSD::array_const_iterator it = root.beginArray(); it != root.endArray(); ++it)
    {
        const LLSD& entry = *it;

        Track track;
        track.uri = entry["uri"].asString();
        track.label = entry["label"].asString();
        track.sourceLabel = entry["source_label"].asString();
        track.folderPath = entry["folder_path"].asString();
        if (track.uri.empty())
        {
            continue;
        }
        track.id = mNextTrackId++;

        if (!track.folderPath.empty())
        {
            // S24: re-derive the folder's display label from its own path
            // rather than persisting it separately - same wide/UTF-8-safe
            // conversion addFolder() itself uses, so a saved playlist with
            // non-ASCII folder names round-trips correctly too.
            std::wstring wlabel = std::filesystem::path(ll_convert_string_to_wide(track.folderPath)).filename().wstring();
            std::string folder_label = ll_convert_wide_to_string(wlabel);
            track.folderGroupId = getOrCreateFolderGroupId(track.folderPath, folder_label);
        }

        mPlaylist.push_back(track);
    }
}

void LLFloaterMusicPlayer::updateTransportState()
{
    if (!mPlayPauseBtn)
    {
        return;
    }

    bool is_playing = mMediaPlugin && mCurrentTrack >= 0 && !mIsPaused;
    mPlayPauseBtn->setImageOverlay(is_playing ? "Pause_Off" : "Play_Off");
    mPlayPauseBtn->setToolTip(std::string(is_playing ? "Pause" : "Play"));
}

void LLFloaterMusicPlayer::updateNowPlayingLabel()
{
    if (!mNowPlayingText)
    {
        return;
    }

    if (!mMediaPlugin || mCurrentTrack < 0)
    {
        mNowPlayingText->setText(LLStringExplicit("Not playing"));
        return;
    }

    // S24: the libvlc plugin already extracts Shoutcast/Icecast ICY
    // title/artist tags for internet radio - surface them here when
    // present (Phase 2), falling back to the playlist entry's own label
    // (a local file's name, or the raw URL) otherwise.
    std::string title = mMediaPlugin->getMetadataTitle();
    std::string artist = mMediaPlugin->getMetadataArtist();

    std::string now_playing;
    if (!title.empty() && !artist.empty())
    {
        now_playing = artist + " - " + title;
    }
    else if (!title.empty())
    {
        now_playing = title;
    }
    else
    {
        now_playing = mPlaylist[mCurrentTrack].label;
    }

    if (mIsPaused)
    {
        now_playing += " (paused)";
    }

    mNowPlayingText->setText(LLStringExplicit(now_playing));
}

// static
void LLFloaterMusicPlayer::idle(void* user_data)
{
    LLFloaterMusicPlayer* self = (LLFloaterMusicPlayer*)user_data;
    if (!self->mMediaPlugin)
    {
        return;
    }

    // S24: pumps the plugin's own message queue - the SAME call
    // LLStreamingAudio_MediaPlugins::update() makes every frame
    // (llviewermedia_streamingaudio.cpp) for parcel audio's own identical
    // headless plugin usage.
    self->mMediaPlugin->idle();

    if (self->mCurrentTrack >= 0 && !self->mIsPaused)
    {
        LLPluginClassMediaOwner::EMediaStatus status = self->mMediaPlugin->getStatus();
        bool is_terminal = (status == LLPluginClassMediaOwner::MEDIA_DONE ||
                             status == LLPluginClassMediaOwner::MEDIA_ERROR);

        // S24: getStatus() still reports the PREVIOUS track's terminal
        // status (DONE or ERROR) until the plugin's async reply to the
        // loadURI()/start() just issued by playTrack() catches up - only
        // clear the guard once status has actually moved to a genuine
        // in-progress state (LOADING/LOADED/PLAYING/PAUSED/NONE), never
        // merely "not equal to the exact terminal value we started from"
        // (a stale MEDIA_ERROR would otherwise look "fresh" against a
        // guard that was only checking for MEDIA_DONE, and re-trigger the
        // exact same chain-skip this guard exists to prevent).
        if (self->mAwaitingFreshStatus && !is_terminal)
        {
            self->mAwaitingFreshStatus = false;
        }

        if (!self->mAwaitingFreshStatus && is_terminal)
        {
            // S24: track finished naturally, or genuinely failed to load
            // (e.g. an unreadable file swept in via Add Folder) - either
            // way, advance rather than sit stuck on it. playTrack()
            // re-asserts the parcel-audio guard for the new track itself,
            // so nothing else to do here.
            self->onNext();
            return;
        }
    }

    self->updateNowPlayingLabel();
}
