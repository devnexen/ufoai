/**
 * @file r_model_alias.h
 * @brief Shared alias model functions
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

#define MODEL_MAX_PATH 64

#define mAliasCoord_t vec2_t

typedef	struct	mAliasVertex_s {
	vec3_t	point;
	vec3_t	normal;
} mAliasVertex_t;

typedef	struct mAliasFrame_s {
	vec3_t	mins, maxs;
	vec3_t	translate;
	vec3_t	scale;
	float	radius;
} mAliasFrame_t;

typedef struct mAliasTagOrientation_s {
	vec3_t origin;
	float axis[3][3];
} mAliasTagOrientation_t;

typedef	struct mAliasTag_s {
	char	name[MODEL_MAX_PATH];
	mAliasTagOrientation_t	orient;
} mAliasTag_t;

typedef	struct mAliasSkin_s {
	char	name[MODEL_MAX_PATH];
	int	shader;
	image_t *skin;
} mAliasSkin_t;

typedef	struct mAliasMesh_s {
	int	num_verts;
	char	name[MODEL_MAX_PATH];
	mAliasVertex_t	*vertexes;
	mAliasCoord_t	*stcoords;

	int	num_tris;
	int32_t	*indexes;

	int		num_skins;
	int		skinHeight;
	int		skinWidth;
	mAliasSkin_t	*skins;
} mAliasMesh_t;

#define MAX_ANIMS		128
#define MAX_ANIMNAME	16

typedef struct mAliasAnim_s {
	char name[MAX_ANIMNAME];
	int from, to;
	int time;
} mAliasAnim_t;

typedef	struct	mAliasModel_s {
	int		num_frames;
	mAliasFrame_t	*frames;

	int		num_tags;
	mAliasTag_t	*tags;

	int		num_meshes;
	mAliasMesh_t	*meshes;

	/** animation data */
	char animname[MAX_QPATH];
	int num_anims;
	mAliasAnim_t *animdata;

	qboolean noshadow;

	/** tag data */
	char tagname[MAX_QPATH];
	void *tagdata;
} mAliasModel_t;

void R_ModLoadAnims(mAliasModel_t * mod, void *buffer);
