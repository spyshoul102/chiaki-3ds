// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_HOST_H
#define CHIAKI_HOST_H

#include <string>
#include <map>
#include <netinet/in.h>

#include <chiaki/log.h>
#include <chiaki/regist.h>
#include <chiaki/discovery.h>
#include <chiaki/opusdecoder.h>
#include <chiaki/controller.h>

#include "exception.h"
#include "io.h"
#include "settings.h"

class DiscoveryManager;
static void Discovery(ChiakiDiscoveryHost*, void *);
static void InitAudioCB(unsigned int channels, unsigned int rate, void * user);
static bool VideoCB(uint8_t * buf, size_t buf_size, void * user);
static void AudioCB(int16_t * buf, size_t samples_count, void * user);
static void RegistEventCB(ChiakiRegistEvent * event, void * user);
static void EventCB(ChiakiEvent * event, void * user);

enum RegistError
{
	HOST_REGISTER_OK,
	HOST_REGISTER_ERROR_SETTING_PSNACCOUNTID,
	HOST_REGISTER_ERROR_SETTING_PSNONLINEID
};

class Settings;

class Host
{
	private:
		ChiakiLog * log = nullptr;
		Settings * settings = nullptr;
		//video config
		ChiakiVideoResolutionPreset video_resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
		ChiakiVideoFPSPreset video_fps = CHIAKI_VIDEO_FPS_PRESET_60;
		// info from discovery manager
		int device_discovery_protocol_version = 0;
		std::string host_type;
		// user info
		std::string psn_online_id = "";
		std::string psn_account_id = "";
		// info from regist/settings
		ChiakiRegist regist = {};
		ChiakiRegistInfo regist_info = {};
		std::function<void()> chiaki_regist_event_type_finished_canceled = nullptr;
		std::function<void()> chiaki_regist_event_type_finished_failed = nullptr;
		std::function<void()> chiaki_regist_event_type_finished_success = nullptr;

		std::string ap_ssid;
		std::string ap_bssid;
		std::string ap_key;
		std::string ap_name;
		std::string ps4_nickname;
		// mac address = 48 bits
		uint8_t ps4_mac[6] = {0};
		char rp_regist_key[CHIAKI_SESSION_AUTH_SIZE] = {0};
		uint32_t rp_key_type = 0;
		uint8_t rp_key[0x10] = {0};
		// manage stream session
		bool session_init = false;
		ChiakiSession session;
		ChiakiOpusDecoder opus_decoder;
		ChiakiConnectVideoProfile video_profile;
		ChiakiControllerState keyboard_state;
		friend class DiscoveryManager;
		friend class Settings;
	public:
		// internal state
		ChiakiDiscoveryHostState state = CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN;
		bool discovered = false;
		bool registered = false;
		// rp_key_data is true when rp_key, rp_regist_key, rp_key_type
		bool rp_key_data = false;
		std::string host_name;
		int system_version = 0;
		// sony's host_id == mac addr without colon
		std::string host_id;
		std::string host_addr;
		Host(ChiakiLog * log, Settings * settings, std::string host_name);
		~Host();
		bool GetVideoResolution(int * ret_width, int * ret_height);
		int Register(std::string pin);
		int Wakeup();
		int InitSession(IO *);
		int FiniSession();
		void StopSession();
		void StartSession();
		void SendFeedbackState(ChiakiControllerState*);
		void RegistCB(ChiakiRegistEvent *);

		void SetRegistEventTypeFinishedCanceled(std::function<void()> chiaki_regist_event_type_finished_canceled)
		{
			this->chiaki_regist_event_type_finished_canceled = chiaki_regist_event_type_finished_canceled;
		};
		void SetRegistEventTypeFinishedFailed(std::function<void()> chiaki_regist_event_type_finished_failed)
		{
			this->chiaki_regist_event_type_finished_failed = chiaki_regist_event_type_finished_failed;
		};
		void SetRegistEventTypeFinishedSuccess(std::function<void()> chiaki_regist_event_type_finished_success)
		{
			this->chiaki_regist_event_type_finished_success = chiaki_regist_event_type_finished_success;
		};
};

#endif
