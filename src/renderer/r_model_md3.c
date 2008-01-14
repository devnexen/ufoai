/**
 * @file r_model_md3.c
 * @brief md3 alias model loading
 */

/*
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

#include "r_local.h"

/*
==============================================================================
MD3 ALIAS MODELS
==============================================================================
*/

/**
 * @brief Load MD3 models from file.
 * @note Some Vic code here not fully used
 */
void R_ModLoadAliasMD3Model (model_t *mod, void *buffer, int bufSize)
{
	int					version, i, j, l;
	dmd3_t				*md3;
	dmd3frame_t			*pinframe;
	dmd3tag_t			*pintag;
	dmd3mesh_t			*pinmesh;
	dmd3skin_t			*pinskin;
	dmd3coord_t			*pincoord;
	dmd3vertex_t		*pinvert;
	int32_t				*pinindex, *poutindex;
	mAliasVertex_t		*poutvert;
	mAliasMesh_t		*poutmesh;
	mAliasTag_t			*pouttag;
	mAliasFrame_t		*poutframe;
	float				lat, lng;

	md3 = (dmd3_t *)buffer;
	version = LittleLong(md3->version);

	if (version != MD3_ALIAS_VERSION) {
		Com_Error(ERR_DROP, "%s has wrong version number (%i should be %i)",
				mod->name, version, MD3_ALIAS_VERSION);
	}

	mod->type = mod_alias_md3;
	/* byte swap the header fields and sanity check */
	mod->alias.num_frames = LittleLong(md3->num_frames);
	mod->alias.num_tags = LittleLong(md3->num_tags);
	mod->alias.num_meshes = LittleLong(md3->num_meshes);

	if (mod->alias.num_frames <= 0)
		Sys_Error("model %s has no frames", mod->name);
	else if (mod->alias.num_frames > MD3_MAX_FRAMES)
		Sys_Error("model %s has too many frames", mod->name);

	if (mod->alias.num_tags > MD3_MAX_TAGS)
		Sys_Error("model %s has too many tags", mod->name);
	else if (mod->alias.num_tags < 0)
		Sys_Error("model %s has invalid number of tags", mod->name);

	if (mod->alias.num_meshes <= 0)
		Sys_Error("model %s has no meshes", mod->name);
	else if (mod->alias.num_meshes > MD3_MAX_MESHES)
		Sys_Error("model %s has too many meshes", mod->name);

	/* load the frames */
	pinframe = (dmd3frame_t *)((byte *)md3 + LittleLong(md3->ofs_frames));
	poutframe = mod->alias.frames = VID_TagAlloc(vid_modelPool, sizeof(mAliasFrame_t) * mod->alias.num_frames, 0);

	mod->radius = 0;
	ClearBounds(mod->mins, mod->maxs);

	for (i = 0; i < mod->alias.num_frames; i++, pinframe++, poutframe++) {
		for (j = 0; j < 3; j++) {
			poutframe->mins[j] = LittleFloat(pinframe->mins[j]);
			poutframe->maxs[j] = LittleFloat(pinframe->maxs[j]);
			poutframe->translate[j] = LittleFloat(pinframe->translate[j]);
		}

		poutframe->radius = LittleFloat(pinframe->radius);
		mod->radius = max(mod->radius, poutframe->radius);
		AddPointToBounds(poutframe->mins, mod->mins, mod->maxs);
		AddPointToBounds(poutframe->maxs, mod->mins, mod->maxs);
	}

	/* load the tags */
	if (mod->alias.num_tags) {
		pintag = (dmd3tag_t *)((byte *)md3 + LittleLong(md3->ofs_tags));
		pouttag = mod->alias.tags = VID_TagAlloc(vid_modelPool, sizeof(mAliasTag_t) * mod->alias.num_frames * mod->alias.num_tags, 0);

		for (i = 0; i < mod->alias.num_frames; i++) {
			for (l = 0; l < mod->alias.num_tags; l++, pintag++, pouttag++) {
				memcpy(pouttag->name, pintag->name, MD3_MAX_PATH);
				for (j = 0; j < 3; j++) {
					pouttag->orient.origin[j] = LittleFloat(pintag->orient.origin[j] );
					pouttag->orient.axis[0][j] = LittleFloat(pintag->orient.axis[0][j] );
					pouttag->orient.axis[1][j] = LittleFloat(pintag->orient.axis[1][j] );
					pouttag->orient.axis[2][j] = LittleFloat(pintag->orient.axis[2][j] );
				}
				Com_Printf("X: (%f %f %f) Y: (%f %f %f) Z: (%f %f %f)\n", pouttag->orient.axis[0][0], pouttag->orient.axis[0][1], pouttag->orient.axis[0][2], pouttag->orient.axis[1][0], pouttag->orient.axis[1][1], pouttag->orient.axis[1][2], pouttag->orient.axis[2][0], pouttag->orient.axis[2][1], pouttag->orient.axis[2][2]);
			}
		}
	}

	/* load the meshes */
	pinmesh = (dmd3mesh_t *)((byte *)md3 + LittleLong(md3->ofs_meshes));
	poutmesh = mod->alias.meshes = VID_TagAlloc(vid_modelPool, sizeof(mAliasMesh_t) * mod->alias.num_meshes, 0);

	for (i = 0; i < mod->alias.num_meshes; i++, poutmesh++) {
		memcpy(poutmesh->name, pinmesh->name, MD3_MAX_PATH);

		if (Q_strncmp(pinmesh->id, "IDP3", 4)) {
			Sys_Error("mesh %s in model %s has wrong id (%s should be %i)",
					poutmesh->name, mod->name, pinmesh->id, IDMD3HEADER);
		}

		poutmesh->num_tris = LittleLong(pinmesh->num_tris);
		poutmesh->num_skins = LittleLong(pinmesh->num_skins);
		poutmesh->num_verts = LittleLong(pinmesh->num_verts);

		if (poutmesh->num_skins <= 0)
			Sys_Error("mesh %i in model %s has no skins", i, mod->name);
		else if (poutmesh->num_skins > MD3_MAX_SHADERS)
			Sys_Error("mesh %i in model %s has too many skins", i, mod->name);

		if (poutmesh->num_tris <= 0)
			Sys_Error("mesh %i in model %s has no triangles", i, mod->name);
		else if (poutmesh->num_tris > MD3_MAX_TRIANGLES)
			Sys_Error("mesh %i in model %s has too many triangles", i, mod->name);

		if (poutmesh->num_verts <= 0)
			Sys_Error("mesh %i in model %s has no vertices", i, mod->name);
		else if (poutmesh->num_verts > MD3_MAX_VERTS)
			Sys_Error("mesh %i in model %s has too many vertices", i, mod->name);

		/* register all skins */
		pinskin = (dmd3skin_t *)((byte *)pinmesh + LittleLong(pinmesh->ofs_skins));
		poutmesh->skins = VID_TagAlloc(vid_modelPool, sizeof(mAliasSkin_t) * poutmesh->num_skins, 0);

		for (j = 0; j < mod->alias.meshes[i].num_skins; j++) {
			mod->alias.meshes[i].skins[j].skin = R_AliasModelGetSkin(mod, pinskin->name);
			Q_strncpyz(mod->alias.meshes[i].skins[j].name,
				mod->alias.meshes[i].skins[j].skin->name, MODEL_MAX_PATH);
		}

		/* load the indexes */
		pinindex = (int32_t *)((byte *)pinmesh + LittleLong(pinmesh->ofs_tris));
		poutindex = poutmesh->indexes = VID_TagAlloc(vid_modelPool, sizeof(int32_t) * poutmesh->num_tris * 3, 0);

		for (j = 0; j < poutmesh->num_tris; j++, pinindex += 3, poutindex += 3) {
			poutindex[0] = (int32_t)LittleLong(pinindex[0]);
			poutindex[1] = (int32_t)LittleLong(pinindex[1]);
			poutindex[2] = (int32_t)LittleLong(pinindex[2]);
		}

		/* load the texture coordinates */
		pincoord = (dmd3coord_t *)((byte *)pinmesh + LittleLong(pinmesh->ofs_tcs));
		poutmesh->stcoords = VID_TagAlloc(vid_modelPool, sizeof(mAliasCoord_t) * poutmesh->num_verts, 0);

		for (j = 0; j < poutmesh->num_verts; j++, pincoord++) {
			poutmesh->stcoords[j][0] = LittleFloat(pincoord->st[0]);
			poutmesh->stcoords[j][1] = LittleFloat(pincoord->st[1]);
		}

		/* load the vertexes and normals */
		pinvert = (dmd3vertex_t *)((byte *)pinmesh + LittleLong(pinmesh->ofs_verts));
		poutvert = poutmesh->vertexes = VID_TagAlloc(vid_modelPool, mod->alias.num_frames * poutmesh->num_verts * sizeof(mAliasVertex_t), 0);

		for (l = 0; l < mod->alias.num_frames; l++) {
			for (j = 0; j < poutmesh->num_verts; j++, pinvert++, poutvert++) {
				poutvert->point[0] = LittleShort(pinvert->point[0]) * MD3_XYZ_SCALE;
				poutvert->point[1] = LittleShort(pinvert->point[1]) * MD3_XYZ_SCALE;
				poutvert->point[2] = LittleShort(pinvert->point[2]) * MD3_XYZ_SCALE;

				lat = (pinvert->norm >> 8) & 0xff;
				lng = (pinvert->norm & 0xff);

				lat *= M_PI / 128;
				lng *= M_PI / 128;

				poutvert->normal[0] = cos(lat) * sin(lng);
				poutvert->normal[1] = sin(lat) * sin(lng);
				poutvert->normal[2] = cos(lng);
			}
		}
		pinmesh = (dmd3mesh_t *)((byte *)pinmesh + LittleLong(pinmesh->meshsize));
	}
}
