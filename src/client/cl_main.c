/**
 * @file cl_main.c
 * @brief Primary functions for the client. NB: The main() is system-specific and can currently be found in ports/.
 */

/*
All original material Copyright (C) 2002-2009 UFO: Alien Invasion.

Original file from Quake 2 v3.21: quake2-2.31/client/cl_main.c
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "client.h"
#include "battlescape/cl_localentity.h"
#include "cl_console.h"
#include "cl_screen.h"
#include "cl_game.h"
#include "cl_tutorials.h"
#include "cl_tip.h"
#include "cl_team.h"
#include "cl_language.h"
#include "battlescape/cl_particle.h"
#include "battlescape/cl_actor.h"
#include "battlescape/cl_hud.h"
#include "cl_sequence.h"
#include "battlescape/cl_parse.h"
#include "battlescape/events/e_parse.h"
#include "cl_inventory.h"
#include "battlescape/cl_view.h"
#include "input/cl_joystick.h"
#include "cinematic/cl_cinematic.h"
#include "cl_menu.h"
#include "cl_http.h"
#include "sound/s_music.h"
#include "../shared/infostring.h"
#include "renderer/r_main.h"
#include "renderer/r_particle.h"
#include "menu/m_main.h"
#include "menu/m_popup.h"
#include "menu/m_main.h"
#include "menu/m_font.h"
#include "menu/m_nodes.h"
#include "menu/m_parse.h"
#include "multiplayer/mp_callbacks.h"
#include "multiplayer/mp_serverlist.h"
#include "multiplayer/mp_team.h"

cvar_t *cl_fps;
cvar_t *cl_leshowinvis;
cvar_t *cl_worldlevel;
cvar_t *cl_selected;

cvar_t *cl_lastsave;

static cvar_t *cl_connecttimeout; /* multiplayer connection timeout value (ms) */

static cvar_t *cl_precache;
static cvar_t *cl_introshown;

/* userinfo */
static cvar_t *cl_name;
static cvar_t *cl_msg;
cvar_t *cl_teamnum;
cvar_t *cl_team;

client_static_t cls;
client_state_t cl;

static int precache_check;

static void CL_SpawnSoldiers_f(void);

struct memPool_s *cl_genericPool;	/**< permanent client data - menu, fonts */
struct memPool_s *cl_ircSysPool;	/**< irc pool */
struct memPool_s *cl_soundSysPool;
struct memPool_s *vid_genericPool;	/**< also holds all the static models */
struct memPool_s *vid_imagePool;
struct memPool_s *vid_lightPool;	/**< lightmap - wiped with every new map */
struct memPool_s *vid_modelPool;	/**< modeldata - wiped with every new map */
/*====================================================================== */

/**
 * @brief adds the current command line as a clc_stringcmd to the client message.
 * things like action, turn, etc, are commands directed to the server,
 * so when they are typed in at the console, they will need to be forwarded.
 */
void Cmd_ForwardToServer (void)
{
	const char *cmd = Cmd_Argv(0);
	struct dbuffer *msg;

	if (cls.state <= ca_connected || cmd[0] == '-' || cmd[0] == '+') {
		Com_Printf("Unknown command \"%s\" - wasn't sent to server\n", cmd);
		return;
	}

	msg = new_dbuffer();
	NET_WriteByte(msg, clc_stringcmd);
	dbuffer_add(msg, cmd, strlen(cmd));
	if (Cmd_Argc() > 1) {
		dbuffer_add(msg, " ", 1);
		dbuffer_add(msg, Cmd_Args(), strlen(Cmd_Args()));
	}
	dbuffer_add(msg, "", 1);
	NET_WriteMsg(cls.netStream, msg);
}

/**
 * @brief Set or print some environment variables via console command
 * @sa Sys_Setenv
 */
static void CL_Env_f (void)
{
	const int argc = Cmd_Argc();

	if (argc == 3) {
		Sys_Setenv(Cmd_Argv(1), Cmd_Argv(2));
	} else if (argc == 2) {
		const char *env = SDL_getenv(Cmd_Argv(1));
		if (env)
			Com_Printf("%s=%s\n", Cmd_Argv(1), env);
		else
			Com_Printf("%s undefined\n", Cmd_Argv(1));
	}
}


static void CL_ForwardToServer_f (void)
{
	if (cls.state != ca_connected && cls.state != ca_active) {
		Com_Printf("Can't \"%s\", not connected\n", Cmd_Argv(0));
		return;
	}

	/* don't forward the first argument */
	if (Cmd_Argc() > 1) {
		struct dbuffer *msg;
		msg = new_dbuffer();
		NET_WriteByte(msg, clc_stringcmd);
		dbuffer_add(msg, Cmd_Args(), strlen(Cmd_Args()) + 1);
		NET_WriteMsg(cls.netStream, msg);
	}
}

static void CL_Quit_f (void)
{
	CL_Disconnect();
	Com_Quit();
}

/**
 * @brief Ensures the right menu cvars are set after error drop or map change
 * @note E.g. called after an ERR_DROP was thrown
 * @sa CL_Disconnect
 * @sa SV_Map
 */
void CL_Drop (void)
{
	/* drop loading plaque */
	SCR_EndLoadingPlaque();

	GAME_Drop();
}

/**
 * @note Only call @c CL_Connect if there is no connection yet (@c cls.netStream is @c NULL)
 * @sa CL_Disconnect
 * @sa CL_SendChangedUserinfos
 */
static void CL_Connect (void)
{
	userinfoModified = qfalse;

	NET_DatagramSocketClose(cls.netDatagramSocket);
	cls.netDatagramSocket = NULL;

	assert(!cls.netStream);

	if (cls.servername[0]) {
		assert(cls.serverport[0]);
		cls.netStream = NET_Connect(cls.servername, cls.serverport);
	} else
		cls.netStream = NET_ConnectToLoopBack();
	if (cls.netStream) {
		NET_OOB_Printf(cls.netStream, "connect %i \"%s\"\n", PROTOCOL_VERSION, Cvar_Userinfo());
		cls.connectTime = cls.realtime;
	} else {
		if (cls.servername[0]) {
			assert(cls.serverport[0]);
			Com_Printf("Could not connect to %s %s\n", cls.servername, cls.serverport);
		} else {
			Com_Printf("Could not connect to localhost\n");
		}
	}
}

/**
 * @brief Called after tactical missions to wipe away the tactical-mission-only data.
 * @sa CL_ParseServerData
 * @sa CL_Disconnect
 * @sa R_ClearScene
 */
void CL_ClearState (void)
{
	/* wipe the entire cl structure */
	memset(&cl, 0, sizeof(cl));
	cl.cam.zoom = 1.0;
	V_CalcFovX();

	/* wipe the particles with every new map */
	r_numParticles = 0;
}

/**
 * @brief Sets the @c cls.state to @c ca_disconnected and informs the server
 * @sa CL_Disconnect_f
 * @sa CL_Drop
 * @note Goes from a connected state to disconnected state
 * Sends a disconnect message to the server
 * This is also called on @c Com_Error, so it shouldn't cause any errors
 */
void CL_Disconnect (void)
{
	struct dbuffer *msg;

	/* If playing a cinematic, stop it */
	CIN_StopCinematic();

	if (cls.state == ca_disconnected)
		return;

	/* send a disconnect message to the server */
	if (!Com_ServerState()) {
		msg = new_dbuffer();
		NET_WriteByte(msg, clc_stringcmd);
		NET_WriteString(msg, "disconnect");
		NET_WriteMsg(cls.netStream, msg);
		/* make sure, that this is send */
		NET_Wait(0);
	}

	NET_StreamFinished(cls.netStream);
	cls.netStream = NULL;

	CL_ClearState();

	S_Stop();

	CL_SetClientState(ca_disconnected);
	CL_ClearBattlescapeEvents();
}

/* it's dangerous to activate this */
/*#define ACTIVATE_PACKET_COMMAND*/
#ifdef ACTIVATE_PACKET_COMMAND
/**
 * @brief This function allows you to send network commands from commandline
 * @note This function is only for debugging and testing purposes
 * It is dangerous to leave this activated in final releases
 * packet [destination] [contents]
 * Contents allows \n escape character
 */
static void CL_Packet_f (void)
{
	int i, l;
	const char *in;
	char *out;
	struct net_stream *s;

	if (Cmd_Argc() != 4) {
		Com_Printf("Usage: %s <destination> <port> <contents>\n", Cmd_Argv(0));
		return;
	}

	s = NET_Connect(Cmd_Argv(1), Cmd_Argv(2));
	if (!s) {
		Com_Printf("Could not connect to %s at port %s\n", Cmd_Argv(1), Cmd_Argv(2));
		return;
	}

	in = Cmd_Argv(3);

	l = strlen(in);
	for (i = 0; i < l; i++) {
		if (in[i] == '\\' && in[i + 1] == 'n') {
			*out++ = '\n';
			i++;
		} else
			*out++ = in[i];
	}
	*out = 0;

	NET_OOB_Printf(s, "%s %i", out, PROTOCOL_VERSION);
	NET_StreamFinished(s);
}
#endif

/**
 * @brief Responses to broadcasts, etc
 * @sa CL_ReadPackets
 * @sa CL_Frame
 * @sa SVC_DirectConnect
 */
static void CL_ConnectionlessPacket (struct dbuffer *msg)
{
	const char *s;
	const char *c;
	int i;

	s = NET_ReadStringLine(msg);

	Cmd_TokenizeString(s, qfalse);

	c = Cmd_Argv(0);

	Com_DPrintf(DEBUG_CLIENT, "server OOB: %s\n", Cmd_Args());

	/* server connection */
	if (!strncmp(c, "client_connect", 13)) {
		for (i = 1; i < Cmd_Argc(); i++) {
			const char *p = Cmd_Argv(i);
			if (!strncmp(p, "dlserver=", 9)) {
				p += 9;
				Com_sprintf(cls.downloadReferer, sizeof(cls.downloadReferer), "ufo://%s", cls.servername);
				CL_SetHTTPServer(p);
				if (cls.downloadServer[0])
					Com_Printf("HTTP downloading enabled, URL: %s\n", cls.downloadServer);
			}
		}
		if (cls.state == ca_connected) {
			Com_Printf("Dup connect received. Ignored.\n");
			return;
		}
		msg = new_dbuffer();
		NET_WriteByte(msg, clc_stringcmd);
		NET_WriteString(msg, "new");
		NET_WriteMsg(cls.netStream, msg);
		CL_SetClientState(ca_connected);
		return;
	}

	/* remote command from gui front end */
	if (!strncmp(c, "cmd", 3)) {
		if (!NET_StreamIsLoopback(cls.netStream)) {
			Com_Printf("Command packet from remote host. Ignored.\n");
			return;
		}
		s = NET_ReadString(msg);
		Cbuf_AddText(s);
		Cbuf_AddText("\n");
		return;
	}

	/* teaminfo command */
	if (!strncmp(c, "teaminfo", 8)) {
		CL_ParseTeamInfoMessage(msg);
		return;
	}

	/* ping from server */
	if (!strncmp(c, "ping", 4)) {
		NET_OOB_Printf(cls.netStream, "ack");
		return;
	}

	/* echo request from server */
	if (!strncmp(c, "echo", 4)) {
		NET_OOB_Printf(cls.netStream, "%s", Cmd_Argv(1));
		return;
	}

	/* print */
	if (!strncmp(c, "print", 5)) {
		s = NET_ReadString(msg);
		/* special reject messages needs proper handling */
		if (strstr(s, REJ_PASSWORD_REQUIRED_OR_INCORRECT))
			MN_PushMenu("serverpassword", NULL);
		MN_Popup(_("Notice"), _(s));
		return;
	}

	Com_Printf("Unknown command received \"%s\"\n", c);
}

/**
 * @sa CL_ConnectionlessPacket
 * @sa CL_Frame
 * @sa CL_ParseServerMessage
 * @sa NET_ReadMsg
 * @sa SV_ReadPacket
 */
static void CL_ReadPackets (void)
{
	struct dbuffer *msg;
	while ((msg = NET_ReadMsg(cls.netStream))) {
		const int cmd = NET_ReadByte(msg);
		if (cmd == clc_oob)
			CL_ConnectionlessPacket(msg);
		else
			CL_ParseServerMessage(cmd, msg);
		free_dbuffer(msg);
	}
}

/**
 * @brief Prints the current userinfo string to the game console
 * @sa SV_UserInfo_f
 */
static void CL_UserInfo_f (void)
{
	Com_Printf("User info settings:\n");
	Info_Print(Cvar_Userinfo());
}

/**
 * @brief Send the clc_teaminfo command to server
 * @sa GAME_SendCurrentTeamSpawningInfo
 */
static void CL_SpawnSoldiers_f (void)
{
	GAME_SpawnSoldiers();
}

/**
 * @brief
 * @note Called after precache was sent from the server
 * @sa SV_Configstrings_f
 * @sa CL_Precache_f
 */
void CL_RequestNextDownload (void)
{
	unsigned map_checksum = 0;
	unsigned ufoScript_checksum = 0;
	const char *buf;

	if (cls.state != ca_connected) {
		Com_Printf("CL_RequestNextDownload: Not connected (%i)\n", cls.state);
		return;
	}

	/* for singleplayer game this is already loaded in our local server
	 * and if we are the server we don't have to reload the map here, too */
	if (!Com_ServerState()) {
		qboolean day = atoi(cl.configstrings[CS_LIGHTMAP]);

		/* activate the map loading screen for multiplayer, too */
		SCR_BeginLoadingPlaque();

		/* check download */
		if (precache_check == CS_MODELS) { /* confirm map */
			if (cl.configstrings[CS_TILES][0] != '+') {
				if (!CL_CheckOrDownloadFile(va("maps/%s.bsp", cl.configstrings[CS_TILES])))
					return; /* started a download */
			}
			precache_check = CS_MODELS + 1;
		}

		/* map might still be downloading? */
		if (CL_PendingHTTPDownloads())
			return;

		while ((buf = FS_GetFileData("ufos/*.ufo")) != NULL)
			ufoScript_checksum += LittleLong(Com_BlockChecksum(buf, strlen(buf)));
		FS_GetFileData(NULL);

		CM_LoadMap(cl.configstrings[CS_TILES], day, cl.configstrings[CS_POSITIONS], &map_checksum);
		if (!*cl.configstrings[CS_VERSION] || !*cl.configstrings[CS_MAPCHECKSUM]
		 || !*cl.configstrings[CS_UFOCHECKSUM] || !*cl.configstrings[CS_OBJECTAMOUNT]) {
			Com_sprintf(popupText, sizeof(popupText), _("Local game version (%s) differs from the servers"), UFO_VERSION);
			MN_Popup(_("Error"), popupText);
			Com_Error(ERR_DISCONNECT, "Local game version (%s) differs from the servers", UFO_VERSION);
			return;
		/* checksum doesn't match with the one the server gave us via configstring */
		} else if (map_checksum != atoi(cl.configstrings[CS_MAPCHECKSUM])) {
			MN_Popup(_("Error"), _("Local map version differs from server"));
			Com_Error(ERR_DISCONNECT, "Local map version differs from server: %u != '%s'",
				map_checksum, cl.configstrings[CS_MAPCHECKSUM]);
			return;
		/* amount of objects from script files doensn't match */
		} else if (csi.numODs != atoi(cl.configstrings[CS_OBJECTAMOUNT])) {
			MN_Popup(_("Error"), _("Script files are not the same"));
			Com_Error(ERR_DISCONNECT, "Script files are not the same");
			return;
		/* checksum doesn't match with the one the server gave us via configstring */
		} else if (atoi(cl.configstrings[CS_UFOCHECKSUM]) && ufoScript_checksum != atoi(cl.configstrings[CS_UFOCHECKSUM])) {
			Com_Printf("You are using modified ufo script files - might produce problems\n");
		} else if (strncmp(UFO_VERSION, cl.configstrings[CS_VERSION], sizeof(UFO_VERSION))) {
			Com_sprintf(popupText, sizeof(popupText), _("Local game version (%s) differs from the servers (%s)"), UFO_VERSION, cl.configstrings[CS_VERSION]);
			MN_Popup(_("Error"), popupText);
			Com_Error(ERR_DISCONNECT, "Local game version (%s) differs from the servers (%s)", UFO_VERSION, cl.configstrings[CS_VERSION]);
			return;
		}
	}

	V_LoadMedia();

	cl.servercount = atoi(Cmd_Argv(1));

	{
		struct dbuffer *msg = new_dbuffer();
		/* send begin */
		/* this will activate the render process (see client state ca_active) */
		NET_WriteByte(msg, clc_stringcmd);
		/* see CL_StartGame */
		NET_WriteString(msg, va("begin %i\n", cl.servercount));
		NET_WriteMsg(cls.netStream, msg);
	}

	/* for singleplayer the soldiers get spawned here */
	if (GAME_IsSingleplayer())
		GAME_SpawnSoldiers();

	cls.waitingForStart = cls.realtime;
}


/**
 * @brief The server will send this command right before allowing the client into the server
 * @sa CL_StartGame
 * @todo recheck the checksum server side
 * @sa SV_Configstrings_f
 */
static void CL_Precache_f (void)
{
	precache_check = CS_MODELS;

	CL_RequestNextDownload();
}

/**
 * @brief Precache all menu models for faster access
 * @sa CL_PrecacheModels
 */
static void CL_PrecacheCharacterModels (void)
{
	teamDef_t *td;
	int i, j, num;
	char model[MAX_QPATH];
	const char *path;
	float loading = cls.loadingPercent;
	linkedList_t *list;
	const float percent = 55.0f;

	/* search the name */
	for (i = 0, td = csi.teamDef; i < csi.numTeamDefs; i++, td++)
		for (j = NAME_NEUTRAL; j < NAME_LAST; j++) {
			/* no models for this gender */
			if (!td->numModels[j])
				continue;
			/* search one of the model definitions */
			list = td->models[j];
			assert(list);
			for (num = 0; num < td->numModels[j]; num++) {
				assert(list);
				path = (const char*)list->data;
				list = list->next;
				/* register body */
				Com_sprintf(model, sizeof(model), "%s/%s", path, list->data);
				if (!R_RegisterModelShort(model))
					Com_Printf("Com_PrecacheCharacterModels: Could not register model %s\n", model);
				list = list->next;
				/* register head */
				Com_sprintf(model, sizeof(model), "%s/%s", path, list->data);
				if (!R_RegisterModelShort(model))
					Com_Printf("Com_PrecacheCharacterModels: Could not register model %s\n", model);

				/* skip skin */
				list = list->next;

				/* new path */
				list = list->next;

				cls.loadingPercent += percent / (td->numModels[j] * csi.numTeamDefs * NAME_LAST);
				SCR_DrawPrecacheScreen(qtrue);
			}
		}
	/* some genders may not have models - ensure that we do the wanted percent step */
	cls.loadingPercent = loading + percent;
}

/**
 * @brief Precaches all models at game startup - for faster access
 * @todo In case of vid restart due to changed settings the @c vid_genericPool is
 * wiped away, too. So the models has to be reloaded with every map change
 */
static void CL_PrecacheModels (void)
{
	int i;
	float percent = 40.0f;

	if (cl_precache->integer)
		CL_PrecacheCharacterModels(); /* 55% */
	else
		percent = 95.0f;

	for (i = 0; i < csi.numODs; i++) {
		if (csi.ods[i].type[0] == '\0' || csi.ods[i].isDummy)
			continue;

		if (csi.ods[i].model[0] != '\0') {
			cls.modelPool[i] = R_RegisterModelShort(csi.ods[i].model);
			if (cls.modelPool[i])
				Com_DPrintf(DEBUG_CLIENT, "CL_PrecacheModels: Registered object model: '%s' (%i)\n", csi.ods[i].model, i);
		}
		cls.loadingPercent += percent / csi.numODs;
		SCR_DrawPrecacheScreen(qtrue);
	}
}

/**
 * @brief Init function for clients - called after menu was initialized and ufo-scripts were parsed
 * @sa Qcommon_Init
 */
void CL_InitAfter (void)
{
	int i;
	menuOption_t* vidModesOptions;

	/* start the music track already while precaching data */
	S_Frame();

	cls.loadingPercent = 2.0f;

	/* precache loading screen */
	SCR_DrawPrecacheScreen(qtrue);

	/* init irc commands and cvars */
	Irc_Init();

	cls.loadingPercent = 5.0f;
	SCR_DrawPrecacheScreen(qtrue);

	/* preload all models for faster access */
	CL_PrecacheModels(); /* 95% */

	cls.loadingPercent = 100.0f;
	SCR_DrawPrecacheScreen(qtrue);

	vidModesOptions = MN_AllocStaticOption(VID_GetModeNums());
	if (vidModesOptions == NULL)
		return;

	for (i = 0; i < VID_GetModeNums(); i++) {
		menuOption_t *option = &vidModesOptions[i];
		MN_InitOption(option, "",
			va("%i:%i", vid_modes[i].width, vid_modes[i].height),
			va("%i", vid_modes[i].mode));
		if (i > 0)
			(&vidModesOptions[i - 1])->next = option;
	}
	MN_RegisterOption(OPTION_VIDEO_RESOLUTIONS, &vidModesOptions[0]);

	IN_JoystickInitMenu();

	CL_LanguageInit();

	/* now make sure that all the precached models are stored until we quit the game
	 * otherwise they would be freed with every map change */
	R_SwitchModelMemPoolTag();

	if (!cl_introshown->integer) {
		Cbuf_AddText("cinematic intro;");
		Cvar_Set("cl_introshown", "1");
	}
}

/**
 * @brief Called at client startup
 * @note not called for dedicated servers
 * parses all *.ufos that are needed for single- and multiplayer
 * @sa Com_ParseScripts
 * @sa CL_ParseScriptSecond
 * @sa CL_ParseScriptFirst
 * @note Nothing here should depends on items, equipments, actors and all other
 * entities that are parsed in Com_ParseScripts (because maybe items are not parsed
 * but e.g. techs would need those parsed items - thus we have to parse e.g. techs
 * at a later stage)
 * @note This data is persistent until you shutdown the game
 */
void CL_ParseClientData (const char *type, const char *name, const char **text)
{
	if (!strcmp(type, "font"))
		MN_ParseFont(name, text);
	else if (!strcmp(type, "tutorial"))
		TUT_ParseTutorials(name, text);
	else if (!strcmp(type, "menu_model"))
		MN_ParseMenuModel(name, text);
	else if (!strcmp(type, "icon"))
		MN_ParseIcon(name, text);
	else if (!strcmp(type, "particle"))
		CL_ParseParticle(name, text);
	else if (!strcmp(type, "sequence"))
		CL_ParseSequence(name, text);
	else if (!strcmp(type, "music"))
		M_ParseMusic(name, text);
	else if (!strcmp(type, "tips"))
		CL_ParseTipsOfTheDay(name, text);
	else if (!strcmp(type, "language"))
		CL_ParseLanguages(name, text);
	else if (!strcmp(type, "ugv"))
		CL_ParseUGVs(name, text);
	else if (!strcmp(type, "window"))
		MN_ParseMenu(type, name, text);
}

/** @brief Cvars for initial check (popup at first start) */
static cvarList_t checkcvar[] = {
	{"cl_name", NULL, NULL},
	{"s_language", NULL, NULL},

	{NULL, NULL, NULL}
};
/**
 * @brief Check cvars for some initial values that should/must be set
 */
static void CL_CheckCvars_f (void)
{
	int i = 0;

	while (checkcvar[i].name) {
		if (!checkcvar[i].var)
			checkcvar[i].var = Cvar_Get(checkcvar[i].name, "", 0, NULL);
		if (checkcvar[i].var->string[0] == '\0') {
			Com_Printf("%s has no value\n", checkcvar[i].var->name);
			MN_PushMenu("checkcvars", NULL);
			break;
		}
		i++;
	}
}

/**
 * @brief Print the configstrings to game console
 * @sa SV_PrintConfigStrings_f
 */
static void CL_ShowConfigstrings_f (void)
{
	int i;

	for (i = 0; i < MAX_CONFIGSTRINGS; i++) {
		if (cl.configstrings[i][0] == '\0')
			continue;
		Com_Printf("cl.configstrings[%3i]: %s\n", i, cl.configstrings[i]);
	}
}

/**
 * @brief Autocomplete function for some network functions
 * @sa Cmd_AddParamCompleteFunction
 * @todo Extend this for all the servers on the server browser list
 */
static int CL_CompleteNetworkAddress (const char *partial, const char **match)
{
	int i, matches = 0;
	const char *localMatch[MAX_COMPLETE];
	const size_t len = strlen(partial);
	if (!len) {
		/* list them all if there was no parameter given */
		for (i = 0; i < MAX_BOOKMARKS; i++) {
			const char *adrStr = Cvar_GetString(va("adr%i", i));
			if (adrStr[0] != '\0')
				Com_Printf("%s\n", adrStr);
		}
		return 0;
	}

	localMatch[matches] = NULL;

	/* search all matches and fill the localMatch array */
	for (i = 0; i < MAX_BOOKMARKS; i++) {
		const char *adrStr = Cvar_GetString(va("adr%i", i));
		if (adrStr[0] != '\0' && !strncmp(partial, adrStr, len)) {
			Com_Printf("%s\n", adrStr);
			localMatch[matches++] = adrStr;
			if (matches >= MAX_COMPLETE)
				break;
		}
	}

	return Cmd_GenericCompleteFunction(len, match, matches, localMatch);
}

/**
 * @brief Calls all reset functions for all subsystems like production and research
 * also initializes the cvars and commands
 * @sa CL_Init
 */
static void CL_InitLocal (void)
{
	CL_SetClientState(ca_disconnected);
	cls.realtime = Sys_Milliseconds();

	IN_Init();

	CLMN_InitStartup();
	TUT_InitStartup();
	PTL_InitStartup();
	GAME_InitStartup();
	SEQ_InitStartup();
	ACTOR_InitStartup();
	TEAM_InitStartup();
	TOTD_InitStartup();
	HUD_InitStartup();
	INV_InitStartup();
	HTTP_InitStartup();

	/* register our variables */
	cl_precache = Cvar_Get("cl_precache", "1", CVAR_ARCHIVE, "Precache character models at startup - more memory usage but smaller loading times in the game");
	cl_introshown = Cvar_Get("cl_introshown", "0", CVAR_ARCHIVE, "Only show the intro once at the initial start");
	cl_leshowinvis = Cvar_Get("cl_leshowinvis", "0", CVAR_ARCHIVE, "Show invisible local entities as null models");
	cl_fps = Cvar_Get("cl_fps", "0", CVAR_ARCHIVE, "Show frames per second");
	cl_log_battlescape_events = Cvar_Get("cl_log_battlescape_events", "1", 0, "Log all battlescape events to events.log");
	cl_worldlevel = Cvar_Get("cl_worldlevel", "0", 0, "Current worldlevel in tactical mode");
	cl_worldlevel->modified = qfalse;
	cl_selected = Cvar_Get("cl_selected", "0", CVAR_NOSET, "Current selected soldier");
	cl_connecttimeout = Cvar_Get("cl_connecttimeout", "15000", CVAR_ARCHIVE, "Connection timeout for multiplayer connects");
	cl_lastsave = Cvar_Get("cl_lastsave", "", CVAR_ARCHIVE, "Last saved slot - use for the continue-campaign function");
	/* userinfo */
	cl_name = Cvar_Get("cl_name", Sys_GetCurrentUser(), CVAR_USERINFO | CVAR_ARCHIVE, "Playername");
	cl_team = Cvar_Get("cl_team", "1", CVAR_USERINFO, "Humans (0) or aliens (7)");
	cl_teamnum = Cvar_Get("cl_teamnum", "1", CVAR_USERINFO | CVAR_ARCHIVE, "Teamnum for multiplayer teamplay games");
	cl_msg = Cvar_Get("cl_msg", "2", CVAR_USERINFO | CVAR_ARCHIVE, "Sets the message level for server messages the client receives");
	sv_maxclients = Cvar_Get("sv_maxclients", "1", CVAR_SERVERINFO, "If sv_maxclients is 1 we are in singleplayer - otherwise we are multiplayer mode (see sv_teamplay)");

	masterserver_url = Cvar_Get("masterserver_url", MASTER_SERVER, CVAR_ARCHIVE, "URL of UFO:AI masterserver");

	cl_map_debug = Cvar_Get("debug_map", "0", 0, "Activate realtime map debugging options - bitmask. Valid values are 0, 1, 3 and 7");
	cl_le_debug = Cvar_Get("debug_le", "0", 0, "Activates some local entity debug rendering");

	/* register our commands */
	Cmd_AddCommand("check_cvars", CL_CheckCvars_f, "Check cvars like playername and so on");
	Cmd_AddCommand("targetalign", CL_ActorTargetAlign_f, _("Target your shot to the ground"));

	Cmd_AddCommand("cmd", CL_ForwardToServer_f, "Forward to server");
	Cmd_AddCommand("pingservers", CL_PingServers_f, "Ping all servers in local network to get the serverlist");
	Cmd_AddCommand("disconnect", CL_Disconnect_f, "Disconnect from the current server");
	Cmd_AddCommand("connect", CL_Connect_f, "Connect to given ip");
	Cmd_AddParamCompleteFunction("connect", CL_CompleteNetworkAddress);
	Cmd_AddCommand("reconnect", CL_Reconnect_f, "Reconnect to last server");
	Cmd_AddCommand("rcon", CL_Rcon_f, "Execute a rcon command - see rcon_password");
	Cmd_AddParamCompleteFunction("rcon", CL_CompleteNetworkAddress);
	Cmd_AddCommand("cl_userinfo", CL_UserInfo_f, "Prints your userinfo string");
#ifdef ACTIVATE_PACKET_COMMAND
	/* this is dangerous to leave in */
	Cmd_AddCommand("packet", CL_Packet_f, "Dangerous debug function for network testing");
#endif
	Cmd_AddCommand("quit", CL_Quit_f, "Quits the game");
	Cmd_AddCommand("env", CL_Env_f, NULL);

	Cmd_AddCommand("precache", CL_Precache_f, "Function that is called at mapload to precache map data");
	Cmd_AddCommand("spawnsoldiers", CL_SpawnSoldiers_f, "Spawns the soldiers for the selected teamnum");
	Cmd_AddCommand("cl_configstrings", CL_ShowConfigstrings_f, "Print client configstrings to game console");

	/* forward to server commands
	 * the only thing this does is allow command completion
	 * to work -- all unknown commands are automatically
	 * forwarded to the server */
	Cmd_AddCommand("say", NULL, "Chat command");
	Cmd_AddCommand("say_team", NULL, "Team chat command");
	Cmd_AddCommand("players", NULL, "List of team and player name");
#ifdef DEBUG
	Cmd_AddCommand("debug_cgrid", Grid_DumpWholeClientMap_f, "Shows the whole client side pathfinding grid of the current loaded map");
	Cmd_AddCommand("debug_sgrid", Grid_DumpWholeServerMap_f, "Shows the whole server side pathfinding grid of the current loaded map");
	Cmd_AddCommand("debug_croute", Grid_DumpClientRoutes_f, "Shows the whole client side pathfinding grid of the current loaded map");
	Cmd_AddCommand("debug_sroute", Grid_DumpServerRoutes_f, "Shows the whole server side pathfinding grid of the current loaded map");
	Cmd_AddCommand("debug_tus", CL_DumpTUs_f, "Shows a table of the TUs that would be used by the current actor to move relative to its current location");
	Cmd_AddCommand("debug_movemark", CL_DumpMoveMark_f, "Triggers Grid_MoveMark in every direction at the current truePos.");
	Cmd_AddCommand("debug_actorinvlist", NULL, "Shows the inventory list of all actors");
	Cmd_AddCommand("debug_listle", LE_List_f, "Shows a list of current know local entities with type and status");
	Cmd_AddCommand("debug_listlm", LM_List_f, "Shows a list of current know local models");
	/* forward commands again */
	Cmd_AddCommand("debug_edictuse", NULL, "Call the 'use' function of a given edict");
	Cmd_AddCommand("debug_edicttouch", NULL, "Call the 'touch' function of a given edict");
	Cmd_AddCommand("debug_killteam", NULL, "Kills a given team");
	Cmd_AddCommand("debug_stunteam", NULL, "Stuns a given team");
	Cmd_AddCommand("debug_listscore", NULL, "Shows mission-score entries of all team members");
#endif
}

/**
 * @brief Send the userinfo to the server (and to all other clients)
 * when they changed (CVAR_USERINFO)
 * @sa CL_Connect
 */
static void CL_SendChangedUserinfos (void)
{
	/* send a userinfo update if needed */
	if (cls.state >= ca_connected) {
		if (userinfoModified) {
			struct dbuffer *msg = new_dbuffer();
			NET_WriteByte(msg, clc_userinfo);
			NET_WriteString(msg, Cvar_Userinfo());
			NET_WriteMsg(cls.netStream, msg);
			userinfoModified = qfalse;
		}
	}
}

/**
 * @brief Check whether we are in a tactical mission as server or as client
 * @note handles multiplayer and singleplayer
 *
 * @return true when we are in battlefield
 */
qboolean CL_OnBattlescape (void)
{
	/* server_state is set to zero (ss_dead) on every battlefield shutdown */
	if (Com_ServerState())
		return qtrue; /* server */

	/* client */
	if (cls.state >= ca_connected)
		return qtrue;

	return qfalse;
}

/**
 * @sa CL_Frame
 */
static void CL_SendCommand (void)
{
	/* get new key events */
	IN_SendKeyEvents();

	/* process console commands */
	Cbuf_Execute();

	/* send intentions now */
	CL_SendChangedUserinfos();

	/* fix any cheating cvars */
	Cvar_FixCheatVars();

	/* if the local server is running and we aren't connected then connect */
	switch (cls.state) {
	case ca_disconnected:
		if (Com_ServerState()) {
			cls.servername[0] = '\0';
			cls.serverport[0] = '\0';
			CL_SetClientState(ca_connecting);
			userinfoModified = qfalse;
			return;
		}
		break;
	case ca_connecting:
		if (cls.realtime - cls.connectTime > cl_connecttimeout->integer) {
			Com_Error(ERR_DROP, "Server is not reachable");
		}
		break;
	case ca_connected:
		if (cls.waitingForStart) {
			if (cls.realtime - cls.waitingForStart > cl_connecttimeout->integer) {
				Com_Error(ERR_DROP, "Server aborted connection");
			} else {
				Com_sprintf(cls.loadingMessages, sizeof(cls.loadingMessages),
					"%s (%i)", _("Awaiting game start"), (cls.realtime - cls.waitingForStart) / 1000);
				SCR_UpdateScreen();
			}
		}
		break;
	default:
		break;
	}
}

static void CL_CvarCheck (void)
{
	int v;

	/* worldlevel */
	if (cl_worldlevel->modified) {
		int i;
		if (cl_worldlevel->integer < 0)
			Com_Error(ERR_DROP, "CL_CvarCheck: cl_worldlevel is negative\n");

		if (cl_worldlevel->integer >= cl.mapMaxLevel - 1)
			Cvar_SetValue("cl_worldlevel", cl.mapMaxLevel - 1);
		else if (cl_worldlevel->integer < 0)
			Cvar_SetValue("cl_worldlevel", 0);
		for (i = 0; i < cl.mapMaxLevel; i++)
			MN_ExecuteConfunc("deselfloor %i", i + 1);
		for (; i < PATHFINDING_HEIGHT; i++)
			MN_ExecuteConfunc("disfloor %i", i + 1);
		MN_ExecuteConfunc("selfloor %i", cl_worldlevel->integer + 1);
		cl_worldlevel->modified = qfalse;
	}

	/* language */
	if (s_language->modified)
		CL_LanguageTryToSet(s_language->string);

	/* r_mode and fullscreen */
	v = Cvar_GetInteger("mn_vidmode");
	if (v < -1 || v >= VID_GetModeNums()) {
		Com_Printf("Max vid_mode value is %i (%i)\n", VID_GetModeNums(), v);
		v = Cvar_GetInteger("vid_mode");
		Cvar_SetValue("mn_vidmode", v);
	}
	if (v >= 0)
		Cvar_Set("mn_vidmodestr", va("%i*%i", vid_modes[v].width, vid_modes[v].height));
	else
		Cvar_Set("mn_vidmodestr", _("Custom"));
}

/**
 * @brief Sets the client state
 */
void CL_SetClientState (int state)
{
	Com_DPrintf(DEBUG_CLIENT, "CL_SetClientState: Set new state to %i (old was: %i)\n", state, cls.state);
	cls.state = state;

	switch (cls.state) {
	case ca_uninitialized:
		Com_Error(ERR_FATAL, "CL_SetClientState: Don't set state ca_uninitialized\n");
		break;
	case ca_sequence:
		refdef.ready = qtrue;
		break;
	case ca_active:
		cls.waitingForStart = 0;
		break;
	case ca_connecting:
		if (cls.servername[0]) {
			assert(cls.serverport[0]);
			Com_Printf("Connecting to %s %s...\n", cls.servername, cls.serverport);
		} else {
			Com_Printf("Connecting to localhost...\n");
		}
		CL_Connect();
		break;
	case ca_disconnected:
		cls.waitingForStart = 0;
	case ca_connected:
		break;
	default:
		break;
	}
}

/**
 * @sa Qcommon_Frame
 */
void CL_Frame (int now, void *data)
{
	static int lastFrame = 0;
	int delta;

	if (sys_priority->modified || sys_affinity->modified)
		Sys_SetAffinityAndPriority();

	/* decide the simulation time */
	delta = now - lastFrame;
	if (lastFrame)
		cls.frametime = delta / 1000.0;
	else
		cls.frametime = 0;
	cls.realtime = Sys_Milliseconds();
	cl.time = now;
	lastFrame = now;

	/* frame rate calculation */
	if (delta)
		cls.framerate = 1000.0 / delta;

	if (cls.state == ca_connected) {
		/* we run full speed when connecting */
		CL_RunHTTPDownloads();
	}

	/* fetch results from server */
	CL_ReadPackets();

	CL_SendCommand();

	Irc_Logic_Frame();

	IN_Frame();

	/* update camera position */
	CL_CameraMove();

	/* end the rounds when no soldier is alive */
	CL_RunMapParticles();
	CL_ParticleRun();

	/* update the screen */
	SCR_UpdateScreen();

	/* advance local effects for next frame */
	SCR_RunConsole();

	/* LE updates */
	LE_Think();

	/* sound frame */
	S_Frame();

	/* send a new command message to the server */
	CL_SendCommand();
}

/**
 * @sa CL_Frame
 */
void CL_SlowFrame (int now, void *data)
{
	CL_CvarCheck();

	HUD_ActorUpdateCvars();
}

/**
 * @sa CL_Shutdown
 * @sa CL_InitAfter
 */
void CL_Init (void)
{
	/* i18n through gettext */
	char languagePath[MAX_OSPATH];
	cvar_t *fs_i18ndir;

	if (sv_dedicated->integer)
		return;					/* nothing running on the client */

	memset(&cls, 0, sizeof(cls));

	fs_i18ndir = Cvar_Get("fs_i18ndir", "", 0, "System path to language files");
	/* i18n through gettext */
	setlocale(LC_ALL, "C");
	setlocale(LC_MESSAGES, "");
	/* use system locale dir if we can't find in gamedir */
	if (fs_i18ndir->string[0] != '\0')
		Q_strncpyz(languagePath, fs_i18ndir->string, sizeof(languagePath));
	else
		Com_sprintf(languagePath, sizeof(languagePath), "%s/"BASEDIRNAME"/i18n/", FS_GetCwd());
	Com_DPrintf(DEBUG_CLIENT, "...using mo files from %s\n", languagePath);
	bindtextdomain(TEXT_DOMAIN, languagePath);
	bind_textdomain_codeset(TEXT_DOMAIN, "UTF-8");
	/* load language file */
	textdomain(TEXT_DOMAIN);

	cl_genericPool = Mem_CreatePool("Client: Generic");
	cl_soundSysPool = Mem_CreatePool("Client: Sound system");
	cl_ircSysPool = Mem_CreatePool("Client: IRC system");

	/* all archived variables will now be loaded */
	Con_Init();

	CIN_Init();

	VID_Init();
	S_Init();

	SCR_Init();

	SCR_DrawPrecacheScreen(qfalse);

	CL_InitLocal();

	CL_InitParticles();

	CL_ClearState();

	/* Touch memory */
	Mem_TouchGlobal();
}


/**
 * @brief Saves configuration file and shuts the client systems down
 * @todo this is a callback from @c Sys_Quit and @c Com_Error. It would be better
 * to run quit through here before the final handoff to the sys code.
 * @sa Sys_Quit
 * @sa CL_Init
 */
void CL_Shutdown (void)
{
	static qboolean isdown = qfalse;

	if (isdown) {
		printf("recursive shutdown\n");
		return;
	}
	isdown = qtrue;

	GAME_SetMode(GAME_NONE);
	CL_HTTP_Cleanup();
	Irc_Shutdown();
	Con_SaveConsoleHistory();
	Key_WriteBindings("keys.cfg");
	S_Shutdown();
	R_Shutdown();
	MN_Shutdown();
	CIN_Shutdown();
	FS_Shutdown();
}
