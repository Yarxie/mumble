/* Copyright (C) 2005-2011, Thorvald Natvig <thorvald@natvig.com>
   Copyright (C) 2009-2011, Stefan Hacker <dd0t@users.sourceforge.net>

   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.
   - Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.
   - Neither the name of the Mumble Developers nor the names of its
     contributors may be used to endorse or promote products derived from this
     software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "mumble_pch.hpp"

#include "MainWindow.h"
#include "MessageHandler.h"
#include "ServerHandler.h"
#include "Global.h"
#include "ClientUser.h"
#include "Log.h"
#include "ConversionHelpers.h"
#include "Connection.h"
#include "AudioInput.h"
#include "Channel.h"

#define ACTOR_INIT \
	ClientUser *pSrc=NULL; \
	if (msg.has_actor()) \
		pSrc = ClientUser::get(msg.actor()); \
	Q_UNUSED(pSrc);

#define VICTIM_INIT \
	ClientUser *pDst=ClientUser::get(msg.session()); \
	 if (! pDst) { \
		qWarning("MainWindow: Message for nonexistant victim %d.", msg.session()); \
		return; \
	}

#define SELF_INIT \
	ClientUser *pSelf = ClientUser::get(g.uiSession);


MessageHandler::MessageHandler(ServerHandler *serverHandler, QObject *parent)
	: QObject(parent)
	, pshServerHandler(serverHandler) {}

void MessageHandler::customEvent(QEvent *evt) {
	if (evt->type() != SERVERSEND_EVENT) {
			return;
	}

	ServerHandlerMessageEvent *shme=static_cast<ServerHandlerMessageEvent *>(evt);

#ifdef QT_NO_DEBUG
#define MUMBLE_MH_MSG(x) case MessageTypes:: x : { \
		MumbleProto:: x msg; \
		if (msg.ParseFromArray(shme->qbaMsg.constData(), shme->qbaMsg.size())) \
			msg##x(msg); \
		break; \
	}
#else
#define MUMBLE_MH_MSG(x) case MessageTypes:: x : { \
		MumbleProto:: x msg; \
		if (msg.ParseFromArray(shme->qbaMsg.constData(), shme->qbaMsg.size())) { \
			printf("%s:\n", #x); \
			msg.PrintDebugString(); \
			msg##x(msg); \
		} \
		break; \
	}
#endif
	switch (shme->uiType) {
			MUMBLE_MH_ALL
	}


#undef MUMBLE_MH_MSG
}

void MessageHandler::msgAuthenticate(const MumbleProto::Authenticate &) {
	// Not handled on client
}

void MessageHandler::msgBanList(const MumbleProto::BanList &msg) {
	g.mw->msgBanList(msg);
}

void MessageHandler::msgReject(const MumbleProto::Reject &msg) {
	g.mw->msgReject(msg);
}

void MessageHandler::msgServerSync(const MumbleProto::ServerSync &msg) {
	g.mw->msgServerSync(msg);
}


void MessageHandler::msgServerConfig(const MumbleProto::ServerConfig &msg) {
	if (msg.has_welcome_text())
		g.l->log(Log::Information, tr("Welcome message: %1").arg(u8(msg.welcome_text())));
	if (msg.has_max_bandwidth())
		AudioInput::setMaxBandwidth(msg.max_bandwidth());
	if (msg.has_allow_html())
		g.bAllowHTML = msg.allow_html();
	if (msg.has_message_length())
		g.uiMessageLength = msg.message_length();
	if (msg.has_image_message_length())
		g.uiImageLength = msg.image_message_length();
}

void MessageHandler::msgPermissionDenied(const MumbleProto::PermissionDenied &msg) {
	switch (msg.type()) {
		case MumbleProto::PermissionDenied_DenyType_Permission: {
				VICTIM_INIT;
				SELF_INIT;
				Channel *c = Channel::get(msg.channel_id());
				if (! c)
					return;
				QString pname = ChanACL::permName(static_cast<ChanACL::Permissions>(msg.permission()));
				if (pDst == pSelf)
					g.l->log(Log::PermissionDenied, tr("You were denied %1 privileges in %2.").arg(Log::msgColor(pname, Log::Privilege)).arg(Log::formatChannel(c)));
				else
					g.l->log(Log::PermissionDenied, tr("%3 was denied %1 privileges in %2.").arg(Log::msgColor(pname, Log::Privilege)).arg(Log::formatChannel(c)).arg(Log::formatClientUser(pDst, Log::Target)));
			}
			break;
		case MumbleProto::PermissionDenied_DenyType_SuperUser: {
				g.l->log(Log::PermissionDenied, tr("Denied: Cannot modify SuperUser."));
			}
			break;
		case MumbleProto::PermissionDenied_DenyType_ChannelName: {
				g.l->log(Log::PermissionDenied, tr("Denied: Invalid channel name."));
			}
			break;
		case MumbleProto::PermissionDenied_DenyType_TextTooLong: {
				g.l->log(Log::PermissionDenied, tr("Denied: Text message too long."));
			}
			break;
		case MumbleProto::PermissionDenied_DenyType_H9K: {
				if (g.bHappyEaster) {
					unsigned short p;
					QString h, u, pw;
					bool bold = g.s.bDeaf;
					bool bold2 = g.s.bTTS;
					g.sh->getConnectionInfo(h, p, u, pw);
					g.s.bDeaf = false;
					g.s.bTTS = true;
					quint32 oflags = g.s.qmMessages.value(Log::PermissionDenied);
					g.s.qmMessages[Log::PermissionDenied] = (oflags | Settings::LogTTS) & (~Settings::LogSoundfile);
					g.l->log(Log::PermissionDenied, QString::fromAscii(g.ccHappyEaster + 39).arg(u));
					g.s.qmMessages[Log::PermissionDenied] = oflags;
					g.s.bDeaf = bold;
					g.s.bTTS = bold2;
					g.mw->setWindowIcon(QIcon(QLatin1String(g.ccHappyEaster)));
					g.mw->setStyleSheet(QString::fromAscii(g.ccHappyEaster + 82));
					qWarning() << "Happy Easter";
				}
			}
			break;
		case MumbleProto::PermissionDenied_DenyType_TemporaryChannel: {
				g.l->log(Log::PermissionDenied, tr("Denied: Operation not permitted in temporary channel."));
			}
			break;
		case MumbleProto::PermissionDenied_DenyType_MissingCertificate: {
				VICTIM_INIT;
				SELF_INIT;
				if (pDst == pSelf)
					g.l->log(Log::PermissionDenied, tr("You need a certificate to perform this operation."));
				else
					g.l->log(Log::PermissionDenied, tr("%1 does not have a certificate.").arg(Log::formatClientUser(pDst, Log::Target)));
			}
			break;
		case MumbleProto::PermissionDenied_DenyType_UserName: {
				if (msg.has_name())
					g.l->log(Log::PermissionDenied, tr("Invalid username: %1.").arg(u8(msg.name())));
				else
					g.l->log(Log::PermissionDenied, tr("Invalid username."));
			}
			break;
		case MumbleProto::PermissionDenied_DenyType_ChannelFull: {
				g.l->log(Log::PermissionDenied, tr("Channel is full."));
			}
			break;
		default: {
				if (msg.has_reason())
					g.l->log(Log::PermissionDenied, tr("Denied: %1.").arg(u8(msg.reason())));
				else
					g.l->log(Log::PermissionDenied, tr("Permission denied."));
			}
			break;
	}
}

void MessageHandler::msgUDPTunnel(const MumbleProto::UDPTunnel &) {
	// Not handled on client
}

void MessageHandler::msgUserState(const MumbleProto::UserState &msg) {
	g.mw->msgUserState(msg);
}

void MessageHandler::msgUserRemove(const MumbleProto::UserRemove &msg) {
	g.mw->msgUserRemove(msg);
}

void MessageHandler::msgChannelState(const MumbleProto::ChannelState &msg) {
	g.mw->msgChannelState(msg);
}

void MessageHandler::msgChannelRemove(const MumbleProto::ChannelRemove &msg) {
	g.mw->msgChannelRemove(msg);
}

void MessageHandler::msgTextMessage(const MumbleProto::TextMessage &msg) {
	ACTOR_INIT;
	QString target;

	// Silently drop the message if this user is set to "ignore"
	if (pSrc && pSrc->bLocalIgnore)
		return;

	const QString &plainName = pSrc ? pSrc->qsName : tr("Server", "message from");
	const QString &name = pSrc ? Log::formatClientUser(pSrc, Log::Source) : tr("Server", "message from");

	if (msg.tree_id_size() > 0) {
		target += tr("(Tree) ");
	} else if (msg.channel_id_size() > 0) {
		target += tr("(Channel) ");
	}

	g.l->log(Log::TextMessage, tr("%2%1: %3").arg(name).arg(target).arg(u8(msg.message())),
			 tr("Message from %1").arg(plainName));
}

void MessageHandler::msgACL(const MumbleProto::ACL &msg) {
	g.mw->msgACL(msg);
}

void MessageHandler::msgQueryUsers(const MumbleProto::QueryUsers &msg) {
	g.mw->msgQueryUsers(msg);
}

void MessageHandler::msgPing(const MumbleProto::Ping &) {
	// Not handled on client
}

void MessageHandler::msgCryptSetup(const MumbleProto::CryptSetup &msg) {
	ConnectionPtr c = pshServerHandler->cConnection;
	if (! c)
		return;

	if (msg.has_key() && msg.has_client_nonce() && msg.has_server_nonce()) {
		const std::string &key = msg.key();
		const std::string &client_nonce = msg.client_nonce();
		const std::string &server_nonce = msg.server_nonce();
		if (key.size() == AES_BLOCK_SIZE && client_nonce.size() == AES_BLOCK_SIZE && server_nonce.size() == AES_BLOCK_SIZE)
			c->csCrypt.setKey(reinterpret_cast<const unsigned char *>(key.data()), reinterpret_cast<const unsigned char *>(client_nonce.data()), reinterpret_cast<const unsigned char *>(server_nonce.data()));
	} else if (msg.has_server_nonce()) {
		const std::string &server_nonce = msg.server_nonce();
		if (server_nonce.size() == AES_BLOCK_SIZE) {
			c->csCrypt.uiResync++;
			memcpy(c->csCrypt.decrypt_iv, server_nonce.data(), AES_BLOCK_SIZE);
		}
	} else {
		MumbleProto::CryptSetup mpcs;
		mpcs.set_client_nonce(std::string(reinterpret_cast<const char *>(c->csCrypt.encrypt_iv), AES_BLOCK_SIZE));
		g.sh->sendMessage(mpcs);
	}
}

void MessageHandler::msgContextAction(const MumbleProto::ContextAction &) {
	// Not handled on client
}

void MessageHandler::msgContextActionModify(const MumbleProto::ContextActionModify &msg) {
	g.mw->msgContextActionModify(msg);
}

void MessageHandler::msgVersion(const MumbleProto::Version &msg) {
	if (msg.has_version())
		g.sh->uiVersion = msg.version();
	if (msg.has_release())
		g.sh->qsRelease = u8(msg.release());
	if (msg.has_os()) {
		g.sh->qsOS = u8(msg.os());
		if (msg.has_os_version())
			g.sh->qsOSVersion = u8(msg.os_version());
	}
}

void MessageHandler::msgUserList(const MumbleProto::UserList &msg) {
	g.mw->msgUserList(msg);
}

void MessageHandler::msgVoiceTarget(const MumbleProto::VoiceTarget &) {
	// Not handled on client
}

void MessageHandler::msgPermissionQuery(const MumbleProto::PermissionQuery &msg) {
	g.mw->msgPermissionQuery(msg);
}

void MessageHandler::msgCodecVersion(const MumbleProto::CodecVersion &msg) {
	int alpha = msg.has_alpha() ? msg.alpha() : -1;
	int beta = msg.has_beta() ? msg.beta() : -1;
	bool pref = msg.prefer_alpha();

#ifdef USE_OPUS
	g.bOpus = msg.opus();
#endif
	// Workaround for broken 1.2.2 servers
	if (g.sh && g.sh->uiVersion == 0x010202 && alpha != -1 && alpha == beta) {
		if (pref)
			beta = g.iCodecBeta;
		else
			alpha = g.iCodecAlpha;
	}

	if ((alpha != -1) && (alpha != g.iCodecAlpha)) {
		g.iCodecAlpha = alpha;
		if (pref && ! g.qmCodecs.contains(alpha))
			pref = ! pref;
	}
	if ((beta != -1) && (beta != g.iCodecBeta)) {
		g.iCodecBeta = beta;
		if (! pref && ! g.qmCodecs.contains(beta))
			pref = ! pref;
	}
	g.bPreferAlpha = pref;

	int willuse = pref ? g.iCodecAlpha : g.iCodecBeta;

	static bool warned = false;

	if (! g.qmCodecs.contains(willuse)) {
		if (! warned) {
			g.l->log(Log::CriticalError, tr("Unable to find matching CELT codecs with other clients. You will not be able to talk to all users."));
			warned = true;
		}
	} else {
		warned = false;
	}
}

void MessageHandler::msgUserStats(const MumbleProto::UserStats &msg) {
	g.mw->msgUserStats(msg);
}

void MessageHandler::msgRequestBlob(const MumbleProto::RequestBlob &) {
	// Not handled on client
}

void MessageHandler::msgSuggestConfig(const MumbleProto::SuggestConfig &msg) {
	if (msg.has_version() && (msg.version() > MumbleVersion::getRaw())) {
		g.l->log(Log::Warning, tr("The server requests minimum client version %1").arg(MumbleVersion::toString(msg.version())));
	}
	if (msg.has_positional() && (msg.positional() != g.s.doPositionalAudio())) {
		if (msg.positional())
			g.l->log(Log::Warning, tr("The server requests positional audio be enabled."));
		else
			g.l->log(Log::Warning, tr("The server requests positional audio be disabled."));
	}
	if (msg.has_push_to_talk() && (msg.push_to_talk() != (g.s.atTransmit == Settings::PushToTalk))) {
		if (msg.push_to_talk())
			g.l->log(Log::Warning, tr("The server requests Push-to-Talk be enabled."));
		else
			g.l->log(Log::Warning, tr("The server requests Push-to-Talk be disabled."));
	}
}
