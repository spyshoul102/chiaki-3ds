/*
 * This file is part of Chiaki.
 *
 * Chiaki is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Chiaki is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Chiaki.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <streamsession.h>
#include <settings.h>
#include <controllermanager.h>

#include <chiaki/base64.h>

#if CHIAKI_GUI_ENABLE_QT_GAMEPAD
#include <QGamepadManager>
#include <QGamepad>
#endif

#include <QKeyEvent>
#include <QAudioOutput>

#include <cstring>
#include <chiaki/session.h>

StreamSessionConnectInfo::StreamSessionConnectInfo(Settings *settings, QString host, QByteArray regist_key, QByteArray morning)
{
	key_map = settings->GetControllerMappingForDecoding();
	log_level_mask = settings->GetLogLevelMask();
	log_file = CreateLogFilename();
	video_profile = settings->GetVideoProfile();
	this->host = host;
	this->regist_key = regist_key;
	this->morning = morning;
	audio_buffer_size = settings->GetAudioBufferSize();
}

static void AudioSettingsCb(uint32_t channels, uint32_t rate, void *user);
static void AudioFrameCb(int16_t *buf, size_t samples_count, void *user);
static bool VideoSampleCb(uint8_t *buf, size_t buf_size, void *user);
static void EventCb(ChiakiEvent *event, void *user);

StreamSession::StreamSession(const StreamSessionConnectInfo &connect_info, QObject *parent)
	: QObject(parent),
	log(this, connect_info.log_level_mask, connect_info.log_file),
#if CHIAKI_GUI_ENABLE_QT_GAMEPAD
	gamepad(nullptr),
#endif
	controller(nullptr),
	video_decoder(log.GetChiakiLog()),
	audio_output(nullptr),
	audio_io(nullptr)
{
	chiaki_opus_decoder_init(&opus_decoder, log.GetChiakiLog());
	audio_buffer_size = connect_info.audio_buffer_size;

	QByteArray host_str = connect_info.host.toUtf8();

	ChiakiConnectInfo chiaki_connect_info;
	chiaki_connect_info.host = host_str.constData();
	chiaki_connect_info.video_profile = connect_info.video_profile;

	if(connect_info.regist_key.size() != sizeof(chiaki_connect_info.regist_key))
		throw ChiakiException("RegistKey invalid");
	memcpy(chiaki_connect_info.regist_key, connect_info.regist_key.constData(), sizeof(chiaki_connect_info.regist_key));

	if(connect_info.morning.size() != sizeof(chiaki_connect_info.morning))
		throw ChiakiException("Morning invalid");
	memcpy(chiaki_connect_info.morning, connect_info.morning.constData(), sizeof(chiaki_connect_info.morning));

	memset(&keyboard_state, 0, sizeof(keyboard_state));

	ChiakiErrorCode err = chiaki_session_init(&session, &chiaki_connect_info, log.GetChiakiLog());
	if(err != CHIAKI_ERR_SUCCESS)
		throw ChiakiException("Chiaki Session Init failed: " + QString::fromLocal8Bit(chiaki_error_string(err)));

	chiaki_opus_decoder_set_cb(&opus_decoder, AudioSettingsCb, AudioFrameCb, this);
	ChiakiAudioSink audio_sink;
	chiaki_opus_decoder_get_sink(&opus_decoder, &audio_sink);
	chiaki_session_set_audio_sink(&session, &audio_sink);

	chiaki_session_set_video_sample_cb(&session, VideoSampleCb, this);
	chiaki_session_set_event_cb(&session, EventCb, this);

#if CHIAKI_GUI_ENABLE_QT_GAMEPAD
	connect(QGamepadManager::instance(), &QGamepadManager::connectedGamepadsChanged, this, &StreamSession::UpdateGamepads);
#endif
#if CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	connect(ControllerManager::GetInstance(), &ControllerManager::AvailableControllersUpdated, this, &StreamSession::UpdateGamepads);
#endif

	key_map = connect_info.key_map;
	UpdateGamepads();
}

StreamSession::~StreamSession()
{
	chiaki_session_join(&session);
	chiaki_session_fini(&session);
	chiaki_opus_decoder_fini(&opus_decoder);
#if CHIAKI_GUI_ENABLE_QT_GAMEPAD
	delete gamepad;
#endif
#if CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	delete controller;
#endif
}

void StreamSession::Start()
{
	ChiakiErrorCode err = chiaki_session_start(&session);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		chiaki_session_fini(&session);
		throw ChiakiException("Chiaki Session Start failed");
	}
}

void StreamSession::Stop()
{
	chiaki_session_stop(&session);
}

void StreamSession::SetLoginPIN(const QString &pin)
{
	QByteArray data = pin.toUtf8();
	chiaki_session_set_login_pin(&session, (const uint8_t *)data.constData(), data.size());
}

void StreamSession::HandleMouseEvent(QMouseEvent *event)
{
	if(event->type() == QEvent::MouseButtonPress)
		keyboard_state.buttons |= CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
	else
		keyboard_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
	SendFeedbackState();
}

void StreamSession::HandleKeyboardEvent(QKeyEvent *event)
{
	if(key_map.contains(Qt::Key(event->key())) == false)
		return;

	if(event->isAutoRepeat())
		return;

	int button = key_map[Qt::Key(event->key())];
	bool press_event = event->type() == QEvent::Type::KeyPress;

	switch(button)
	{
		case CHIAKI_CONTROLLER_ANALOG_BUTTON_L2:
			keyboard_state.l2_state = press_event ? 0xff : 0;
			break;
		case CHIAKI_CONTROLLER_ANALOG_BUTTON_R2:
			keyboard_state.r2_state = press_event ? 0xff : 0;
			break;
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_Y_UP):
			keyboard_state.right_y = press_event ? -0x3fff : 0;
			break;
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_Y_DOWN):
			keyboard_state.right_y = press_event ? 0x3fff : 0;
			break;
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_X_UP):
			keyboard_state.right_x = press_event ? 0x3fff : 0;
			break;
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_X_DOWN):
			keyboard_state.right_x = press_event ? -0x3fff : 0;
			break;
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_Y_UP):
			keyboard_state.left_y = press_event ? -0x3fff : 0;
			break;
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_Y_DOWN):
			keyboard_state.left_y = press_event ? 0x3fff : 0;
			break;
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_X_UP):
			keyboard_state.left_x = press_event ? 0x3fff : 0;
			break;
		case static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_X_DOWN):
			keyboard_state.left_x = press_event ? -0x3fff : 0;
			break;
		default:
			if(press_event)
				keyboard_state.buttons |= button;
			else
				keyboard_state.buttons &= ~button;
			break;
	}

	SendFeedbackState();
}

void StreamSession::UpdateGamepads()
{
#if CHIAKI_GUI_ENABLE_QT_GAMEPAD
	if(!gamepad || !gamepad->isConnected())
	{
		if(gamepad)
		{
			CHIAKI_LOGI(log.GetChiakiLog(), "Gamepad %d disconnected", gamepad->deviceId());
			delete gamepad;
			gamepad = nullptr;
		}
		const auto connected_pads = QGamepadManager::instance()->connectedGamepads();
		if(!connected_pads.isEmpty())
		{
			gamepad = new QGamepad(connected_pads[0], this);
			CHIAKI_LOGI(log.GetChiakiLog(), "Gamepad %d connected: \"%s\"", connected_pads[0], gamepad->name().toLocal8Bit().constData());
			connect(gamepad, &QGamepad::buttonAChanged, this, &StreamSession::SendFeedbackState);
			connect(gamepad, &QGamepad::buttonBChanged, this, &StreamSession::SendFeedbackState);
			connect(gamepad, &QGamepad::buttonXChanged, this, &StreamSession::SendFeedbackState);
			connect(gamepad, &QGamepad::buttonYChanged, this, &StreamSession::SendFeedbackState);
			connect(gamepad, &QGamepad::buttonLeftChanged, this, &StreamSession::SendFeedbackState);
			connect(gamepad, &QGamepad::buttonRightChanged, this, &StreamSession::SendFeedbackState);
			connect(gamepad, &QGamepad::buttonUpChanged, this, &StreamSession::SendFeedbackState);
			connect(gamepad, &QGamepad::buttonDownChanged, this, &StreamSession::SendFeedbackState);
			connect(gamepad, &QGamepad::buttonL1Changed, this, &StreamSession::SendFeedbackState);
			connect(gamepad, &QGamepad::buttonR1Changed, this, &StreamSession::SendFeedbackState);
			connect(gamepad, &QGamepad::buttonL1Changed, this, &StreamSession::SendFeedbackState);
			connect(gamepad, &QGamepad::buttonL2Changed, this, &StreamSession::SendFeedbackState);
			connect(gamepad, &QGamepad::buttonL3Changed, this, &StreamSession::SendFeedbackState);
			connect(gamepad, &QGamepad::buttonR3Changed, this, &StreamSession::SendFeedbackState);
			connect(gamepad, &QGamepad::buttonStartChanged, this, &StreamSession::SendFeedbackState);
			connect(gamepad, &QGamepad::buttonSelectChanged, this, &StreamSession::SendFeedbackState);
			connect(gamepad, &QGamepad::buttonGuideChanged, this, &StreamSession::SendFeedbackState);
			connect(gamepad, &QGamepad::axisLeftXChanged, this, &StreamSession::SendFeedbackState);
			connect(gamepad, &QGamepad::axisLeftYChanged, this, &StreamSession::SendFeedbackState);
			connect(gamepad, &QGamepad::axisRightXChanged, this, &StreamSession::SendFeedbackState);
			connect(gamepad, &QGamepad::axisRightYChanged, this, &StreamSession::SendFeedbackState);
		}
	}

	SendFeedbackState();
#endif
#if CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	if(!controller || !controller->IsConnected())
	{
		if(controller)
		{
			CHIAKI_LOGI(log.GetChiakiLog(), "Controller %d disconnected", controller->GetDeviceID());
			delete controller;
			controller = nullptr;
		}
		const auto available_controllers = ControllerManager::GetInstance()->GetAvailableControllers();
		if(!available_controllers.isEmpty())
		{
			controller = ControllerManager::GetInstance()->OpenController(available_controllers[0]);
			if(!controller)
			{
				CHIAKI_LOGE(log.GetChiakiLog(), "Failed to open controller %d", available_controllers[0]);
				return;
			}
			CHIAKI_LOGI(log.GetChiakiLog(), "Controller %d opened: \"%s\"", available_controllers[0], controller->GetName().toLocal8Bit().constData());
			connect(controller, &Controller::StateChanged, this, &StreamSession::SendFeedbackState);
		}
	}

	SendFeedbackState();
#endif
}

void StreamSession::SendFeedbackState()
{
	ChiakiControllerState state = {};

#if CHIAKI_GUI_ENABLE_QT_GAMEPAD
	if(gamepad)
	{
		state.buttons |= gamepad->buttonA() ? CHIAKI_CONTROLLER_BUTTON_CROSS : 0;
		state.buttons |= gamepad->buttonB() ? CHIAKI_CONTROLLER_BUTTON_MOON : 0;
		state.buttons |= gamepad->buttonX() ? CHIAKI_CONTROLLER_BUTTON_BOX : 0;
		state.buttons |= gamepad->buttonY() ? CHIAKI_CONTROLLER_BUTTON_PYRAMID : 0;
		state.buttons |= gamepad->buttonLeft() ? CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT : 0;
		state.buttons |= gamepad->buttonRight() ? CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT : 0;
		state.buttons |= gamepad->buttonUp() ? CHIAKI_CONTROLLER_BUTTON_DPAD_UP : 0;
		state.buttons |= gamepad->buttonDown() ? CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN : 0;
		state.buttons |= gamepad->buttonL1() ? CHIAKI_CONTROLLER_BUTTON_L1 : 0;
		state.buttons |= gamepad->buttonR1() ? CHIAKI_CONTROLLER_BUTTON_R1 : 0;
		state.buttons |= gamepad->buttonL3() ? CHIAKI_CONTROLLER_BUTTON_L3 : 0;
		state.buttons |= gamepad->buttonR3() ? CHIAKI_CONTROLLER_BUTTON_R3 : 0;
		state.buttons |= gamepad->buttonStart() ? CHIAKI_CONTROLLER_BUTTON_OPTIONS : 0;
		state.buttons |= gamepad->buttonSelect() ? CHIAKI_CONTROLLER_BUTTON_SHARE : 0;
		state.buttons |= gamepad->buttonGuide() ? CHIAKI_CONTROLLER_BUTTON_PS : 0;
		state.l2_state = (uint8_t)(gamepad->buttonL2() * 0xff);
		state.r2_state = (uint8_t)(gamepad->buttonR2() * 0xff);
		state.left_x = static_cast<int16_t>(gamepad->axisLeftX() * 0x7fff);
		state.left_y = static_cast<int16_t>(gamepad->axisLeftY() * 0x7fff);
		state.right_x = static_cast<int16_t>(gamepad->axisRightX() * 0x7fff);
		state.right_y = static_cast<int16_t>(gamepad->axisRightY() * 0x7fff);
	}
#endif

#if CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	if(controller)
		state = controller->GetState();
#endif

	chiaki_controller_state_or(&state, &state, &keyboard_state);
	chiaki_session_set_controller_state(&session, &state);
}

void StreamSession::InitAudio(unsigned int channels, unsigned int rate)
{
	delete audio_output;
	audio_output = nullptr;
	audio_io = nullptr;

	QAudioFormat audio_format;
	audio_format.setSampleRate(rate);
	audio_format.setChannelCount(channels);
	audio_format.setSampleSize(16);
	audio_format.setCodec("audio/pcm");
	audio_format.setSampleType(QAudioFormat::SignedInt);

	QAudioDeviceInfo audio_device_info(QAudioDeviceInfo::defaultOutputDevice());
	if(!audio_device_info.isFormatSupported(audio_format))
	{
		CHIAKI_LOGE(log.GetChiakiLog(), "Audio Format with %u channels @ %u Hz not supported by Audio Device %s",
					channels, rate,
					audio_device_info.deviceName().toLocal8Bit().constData());
		return;
	}

	audio_output = new QAudioOutput(audio_format, this);
	audio_output->setBufferSize(audio_buffer_size);
	audio_io = audio_output->start();

	CHIAKI_LOGI(log.GetChiakiLog(), "Audio Device %s opened with %u channels @ %u Hz, buffer size %u",
				audio_device_info.deviceName().toLocal8Bit().constData(),
				channels, rate, audio_output->bufferSize());
}

void StreamSession::PushAudioFrame(int16_t *buf, size_t samples_count)
{
	if(!audio_io)
		return;
	audio_io->write((const char *)buf, static_cast<qint64>(samples_count * 2 * 2));
}

void StreamSession::PushVideoSample(uint8_t *buf, size_t buf_size)
{
	video_decoder.PushFrame(buf, buf_size);
}

void StreamSession::Event(ChiakiEvent *event)
{
	switch(event->type)
	{
		case CHIAKI_EVENT_QUIT:
			emit SessionQuit(event->quit.reason, event->quit.reason_str ? QString::fromUtf8(event->quit.reason_str) : QString());
			break;
		case CHIAKI_EVENT_LOGIN_PIN_REQUEST:
			emit LoginPINRequested(event->login_pin_request.pin_incorrect);
			break;
	}
}

class StreamSessionPrivate
{
	public:
		static void InitAudio(StreamSession *session, uint32_t channels, uint32_t rate)
		{
			QMetaObject::invokeMethod(session, "InitAudio", Qt::ConnectionType::BlockingQueuedConnection, Q_ARG(unsigned int, channels), Q_ARG(unsigned int, rate));
		}

		static void PushAudioFrame(StreamSession *session, int16_t *buf, size_t samples_count)	{ session->PushAudioFrame(buf, samples_count); }
		static void PushVideoSample(StreamSession *session, uint8_t *buf, size_t buf_size)		{ session->PushVideoSample(buf, buf_size); }
		static void Event(StreamSession *session, ChiakiEvent *event)							{ session->Event(event); }
};

static void AudioSettingsCb(uint32_t channels, uint32_t rate, void *user)
{
	auto session = reinterpret_cast<StreamSession *>(user);
	StreamSessionPrivate::InitAudio(session, channels, rate);
}

static void AudioFrameCb(int16_t *buf, size_t samples_count, void *user)
{
	auto session = reinterpret_cast<StreamSession *>(user);
	StreamSessionPrivate::PushAudioFrame(session, buf, samples_count);
}

static bool VideoSampleCb(uint8_t *buf, size_t buf_size, void *user)
{
	auto session = reinterpret_cast<StreamSession *>(user);
	StreamSessionPrivate::PushVideoSample(session, buf, buf_size);
	return true;
}

static void EventCb(ChiakiEvent *event, void *user)
{
	auto session = reinterpret_cast<StreamSession *>(user);
	StreamSessionPrivate::Event(session, event);
}
