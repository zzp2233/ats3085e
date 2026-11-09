#include "gx_coreapplication.h"
#include "gx_dir.h"
#include "gx_environment.h"
#include "gx_file.h"
#include "gx_uri.h"
#include "shell/shell.h"
#include "sys/printk.h"
#include <cstddef>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "platform/gx_fs.h"

#include "gx_mediaplayer.h"
#include "gx_file.h"
#include "gx_httpstream.h"
#include "gx_networkmanager.h"
#include <gx_mediastream.h>

#include <os_common_api.h>
#include <soc.h>
#include <media_player.h>
#include <srv_manager.h>
#include <app_manager.h>
#include <sys_manager.h>
#include <stdio.h>
#include <string.h>
#include <stream.h>
#include <media_player.h>
#include <audio_system.h>

#include <gx_logger.h>


//using namespace gx

namespace gx {
static MediaPlayer *runningPlayer = nullptr;

static MediaStream *aMediaStream;

static int url_stream_init(io_stream_t handle, void *param)
{
    if (aMediaStream == nullptr) {
        return  -1;
    }

    handle->total_size = aMediaStream->size();

    LogInfo() << "url_stream_init" << handle->total_size;
    handle->wofs = handle->total_size;
    return 0;
}

static int url_stream_open(io_stream_t handle,stream_mode mode)
{
    handle->mode = mode;

    return 0;
}

static int url_stream_read(io_stream_t handle, unsigned char *buf, int len)
{
    int ret = 0;
    int read_cnt = 0;

    if (aMediaStream == nullptr) {
        return  -1;
    }

TRY_AGAIN:
    ret = aMediaStream->read(buf, len);
    read_cnt++;

    LogInfo() << "url_stream_read" << ret;
    if(ret < 0 && read_cnt < 3) {
        goto TRY_AGAIN;
    }

    handle->rofs += ret;
    return ret;
}


static int url_stream_seek(io_stream_t handle, int offset,seek_dir origin)
{
    int ret = 0;

    if (aMediaStream == nullptr) {
        return  -1;
    }

    if ((offset == (int)handle->total_size && origin == SEEK_DIR_BEG) ||
       (offset == (int)handle->rofs && origin == SEEK_DIR_BEG)) {
        LogInfo() << "url_stream_seek miss" << offset << origin << ret;
        return 0;
    }

    ret = aMediaStream->seekByWhence( offset, origin);

    LogInfo() << "url_stream_seek" << offset << origin << ret;
    handle->rofs = offset;
    return 0;

}

static int url_stream_tell(io_stream_t handle)
{
    int ret = 0;
    if (aMediaStream == nullptr) {
        return  -1;
    }
    ret = aMediaStream->tell();

    LogInfo() << "url_stream_tell" << ret;
    return ret;

}


static int url_stream_get_length(io_stream_t handle)
{
    if (aMediaStream == nullptr) {
        return  -1;
    }
    return aMediaStream->size();

}

static int url_stream_close(io_stream_t handle)
{
    handle->wofs = 0;
    handle->rofs = 0;
    handle->total_size = 0;
    handle->state = STATE_CLOSE;

    return 0;
}

static int url_stream_destroy(io_stream_t handle)
{
    return 0;
}

const stream_ops_t url_stream_ops = {
    .init = url_stream_init,
    .open = url_stream_open,
    .read = url_stream_read,
    .seek = url_stream_seek,
    .tell = url_stream_tell,
    .get_length = url_stream_get_length,
    .close = url_stream_close,
    .destroy = url_stream_destroy,

};

io_stream_t url_stream_create(void )
{
    return stream_create(&url_stream_ops, NULL);
}

static io_stream_t open_url_stream( stream_mode  mode)
{
    io_stream_t stream = url_stream_create();

    if (!stream)
        return NULL;

    if (stream_open(stream, mode)) {
        stream_destroy(stream);
        return NULL;
    }

    return stream;
}

static io_stream_t dec_in_stream;

static void _actsmusicplayer_event_callback(u32_t event, void *data, u32_t len, void *user_data)
{

    LogInfo() << "ActsMusicPlayer callback" << event;

	if (!runningPlayer)
		return;

	switch (event) {
	case PLAYBACK_EVENT_STOP_ERROR:
		break;
	case PLAYBACK_EVENT_STOP_COMPLETE:
	//case PARSER_EVENT_STOP_COMPLETE:
        runningPlayer->stop();
		break;
	default:

		break;
	}
}

media_player_t *ActsMusicPlayerCreate()
{
    media_init_param_t init_param;
    media_player_t *player = NULL;

    LogInfo() << "ActsMusicPlayerCreate";

    memset(&init_param, 0, sizeof(init_param));

    dec_in_stream = open_url_stream(MODE_IN);
    if (!dec_in_stream) {
        //SYS_LOG_ERR("stream open failed \n");
        return nullptr;
    }

    init_param.type = MEDIA_SRV_TYPE_PARSER_AND_PLAYBACK;
    init_param.stream_type = AUDIO_STREAM_LOCAL_MUSIC;
    init_param.format = MP3_TYPE;
    init_param.input_stream = dec_in_stream;
    init_param.output_stream = NULL;
    init_param.event_notify_handle = _actsmusicplayer_event_callback;
    init_param.dsp_output = 1;

    player = media_player_open(&init_param);
    if (!player) {
        //SYS_LOG_ERR("media_player_open failed\n");
        return nullptr;
    }
    return player;
}


int ActsMusicPlayerStart(media_player_t *player)
{
    if (!player)
        return -EFAULT;

    LogInfo() << "ActsMusicPlayerStart";

    return media_player_play(player);
}

int ActsMusicPlayerPaused(media_player_t *player)
{
    if (!player)
        return -EFAULT;

    LogInfo() << "ActsMusicPlayerPaused";

    return media_player_pause(player);
}


int ActsMusicPlayerResume(media_player_t *player)
{
    if (!player)
        return -EFAULT;

    LogInfo() << "ActsMusicPlayerResume";

    return media_player_resume(player);
}

int ActsMusicPlayerStop(media_player_t *player)
{
    if (!player)
        return -1;
    LogInfo() << "ActsMusicPlayerStop";

    media_player_stop(player);
    return 0;
}

int ActsMusicPlayerDestory(media_player_t *player)
{
    if (!player)
        return -1;

    LogInfo() << "acts_PlayMusic_destory" << player;

    media_player_close(player);

    if (dec_in_stream) {
        stream_close(dec_in_stream);
        stream_destroy(dec_in_stream);
        dec_in_stream = NULL;
    }

    return 0;
}

class ACTSMediaPlayer : public MediaPlayer {
    public:
        enum PlayStatus { PlayerPlay = 0, PlayerPause, PlayerStop };

        ACTSMediaPlayer()  : m_playStatus() , m_music() {}

        ~ACTSMediaPlayer() override {
            if (m_music) {
                ActsMusicPlayerDestory(m_music);
                delete aMediaStream;
                aMediaStream = nullptr;
                m_music = nullptr;
                runningPlayer = nullptr;
            }
            notify(OnEnded, 0);
        }

        class ACTSMEDIASTREAM : public MediaStream {
        public:
            static MediaStream *create(const String &uri) {
                if (aMediaStream) {
                    delete aMediaStream;
                }

                aMediaStream = new MediaStream();
                int rc = aMediaStream->open(uri);
                LogInfo() << "aMediaStream" << rc;
                if (rc < 0) {
                    delete aMediaStream;
                    aMediaStream = nullptr;

                    return nullptr;
                }
                return aMediaStream;
            }

        private:
            static int fileClose(MediaStream *ops) {
                delete static_cast<MediaStream *>(ops);
                delete aMediaStream;
                aMediaStream = nullptr;

                return 0;
            }
        };

        media_player_t *get_mplayer() {
            return m_music;
        }

        int play(const String &url) override {
            if (runningPlayer ) {
                if (url != m_url || runningPlayer != this) {
                    runningPlayer->notify(OnInterrupt, 2);
                    runningPlayer->stop();
                    ActsMusicPlayerDestory((static_cast<ACTSMediaPlayer *>(runningPlayer))->get_mplayer());
                    m_music = nullptr;
                }
            }

            runningPlayer = this;

            if (url != m_url) { // play a new music.
                auto *rwOps = ACTSMEDIASTREAM::create(url);
                if (!rwOps) {
                    notify(OnError, 0);
                    return 15001;
                }
                m_url = url;
                m_music = ActsMusicPlayerCreate();
                ActsMusicPlayerStart(m_music);
                m_playStatus = PlayerPlay;
            } else if (m_playStatus == PlayerPause) {
                ActsMusicPlayerResume(m_music);
                m_playStatus = PlayerPlay;
                return 0;
            } else if (m_playStatus == PlayerStop) {
                ActsMusicPlayerDestory(m_music);
                m_music = nullptr;

                auto *rwOps2 = ACTSMEDIASTREAM::create(url);
                if (!rwOps2) {
                    notify(OnError, 0);
                    return 15001;
                }

                m_url = url;
                m_music = ActsMusicPlayerCreate();
                ActsMusicPlayerStart(m_music);
                m_playStatus = PlayerPlay;
            }

            if (!m_music) {
                LogError() << Logger::nospace  << ": " << m_url;
                if (aMediaStream) {
                    delete aMediaStream;
                    aMediaStream = nullptr;
                }
                notify(OnError, 0);
                return 15001;
            }

            notify(OnPlay, 0);

            return 0;
        }

        void pause() override {
            if (m_playStatus != PlayerPlay) {
                LogInfo() << "pause: player status not playing " << m_playStatus;
                return ;
            }

            ActsMusicPlayerPaused(m_music);
            if (m_playStatus != PlayerPause)
                notify(OnPause, 0);

            m_playStatus = PlayerPause;
        }

        void stop() override {
            if (m_playStatus == PlayerStop) {
                LogInfo() << "stop: player status already stop " << m_playStatus;
                return ;
            }

            ActsMusicPlayerStop(m_music);
            if (m_playStatus != PlayerStop)
                notify(OnStop, 0);

            m_playStatus = PlayerStop;
        }

        double position() const override {
            return 0 ;}
        double duration() const override {
            media_info_t info;
            if(media_player_get_mediainfo(m_music, &info) == 0){
                return info.total_time / 1000;
            }
            return 0;
        }
        void setPosition(double position) override {}

    private:
        int m_playStatus;
        String m_url;
        media_player_t *m_music;
};

class ACTSMediaWrapper : public MediaWrapper {
public:
    ACTSMediaWrapper() : MediaWrapper() {}
    virtual ~ACTSMediaWrapper() {}
    virtual MediaPlayer *createPlayer() { return new ACTSMediaPlayer; }
    virtual void setVolume(float volume) { audio_system_set_stream_volume(AUDIO_STREAM_LOCAL_MUSIC, (int)(volume * 100)); }
};

MediaWrapper *MediaWrapper::create() { return new ACTSMediaWrapper(); }

} // namespace gx


