/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2000-2006 Tim Angus

This file is part of Tremfusion.

Tremfusion is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Tremfusion is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Tremfusion; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
#include "tr_local.h"


/*

  for a projection shadow:

  point[x] += light vector * ( z - shadow plane )
  point[y] +=
  point[z] = shadow plane

  1 0 light[x] / light[z]

*/

typedef struct {
	int		i2;
	int		facing;
} edgeDef_t;

#define	MAX_EDGE_DEFS	32

static	edgeDef_t	*edgeDefs;
static	int		*numEdgeDefs;
static	int		*facing;

void R_AddEdgeDef( int i1, int i2, int facing ) {
	int		c;

	c = numEdgeDefs[ i1 ];
	if ( c == MAX_EDGE_DEFS ) {
		return;		// overflow
	}
	edgeDefs[ i1 * MAX_EDGE_DEFS + c ].i2 = i2;
	edgeDefs[ i1 * MAX_EDGE_DEFS + c ].facing = facing;

	numEdgeDefs[ i1 ]++;
}

void R_RenderShadowEdges( void ) {
	int		i;

#if 0
	int		numTris;

	// dumb way -- render every triangle's edges
	numTris = tess.numIndexes / 3;

	for ( i = 0 ; i < numTris ; i++ ) {
		int		i1, i2, i3;

		if ( !facing[i] ) {
			continue;
		}

		if ( tess.indexInc == sizeof( GLuint ) ) {
			GLuint *indexPtr32 = (GLuint *)tess.indexPtr;
			i1 = indexPtr32[ i*3 + 0 ];
			i2 = indexPtr32[ i*3 + 1 ];
			i3 = indexPtr32[ i*3 + 2 ];
		} else {
			i1 = tess.indexPtr[ i*3 + 0 ];
			i2 = tess.indexPtr[ i*3 + 1 ];
			i3 = tess.indexPtr[ i*3 + 2 ];
		}

		qglBegin( GL_TRIANGLE_STRIP );
		qglVertex3fv( tess.vertexPtr[i1].xyz );
		qglVertex3fv( tess.vertexPtr[i1 + tess.numVertexes].xyz );
		qglVertex3fv( tess.vertexPtr[i2].xyz );
		qglVertex3fv( tess.vertexPtr[i2 + tess.numVertexes].xyz );
		qglVertex3fv( tess.vertexPtr[i3].xyz );
		qglVertex3fv( tess.vertexPtr[i3 + tess.numVertexes].xyz );
		qglVertex3fv( tess.vertexPtr[i1].xyz );
		qglVertex3fv( tess.vertexPtr[i1 + tess.numVertexes].xyz );
		qglEnd();
	}
#else
	int		c, c2;
	int		j, k;
	int		i2;
	int		c_edges, c_rejected;
	int		hit[2];

	// an edge is NOT a silhouette edge if its face doesn't face the light,
	// or if it has a reverse paired edge that also faces the light.
	// A well behaved polyhedron would have exactly two faces for each edge,
	// but lots of models have dangling edges or overfanned edges
	c_edges = 0;
	c_rejected = 0;

	for ( i = 0 ; i < tess.numVertexes ; i++ ) {
		c = numEdgeDefs[ i ];
		for ( j = 0 ; j < c ; j++ ) {
			if ( !edgeDefs[ i * MAX_EDGE_DEFS + j ].facing ) {
				continue;
			}

			hit[0] = 0;
			hit[1] = 0;

			i2 = edgeDefs[ i * MAX_EDGE_DEFS + j ].i2;
			c2 = numEdgeDefs[ i2 ];
			for ( k = 0 ; k < c2 ; k++ ) {
				if ( edgeDefs[ i2 * MAX_EDGE_DEFS + k ].i2 == i ) {
					hit[ edgeDefs[ i2 * MAX_EDGE_DEFS + k ].facing ]++;
				}
			}

			// if it doesn't share the edge with another front facing
			// triangle, it is a sil edge
			if ( hit[ 1 ] == 0 ) {
				qglBegin( GL_TRIANGLE_STRIP );
				qglVertex3fv( tess.vertexPtr[i].xyz );
				qglVertex3fv( tess.vertexPtr[i + tess.numVertexes].xyz );
				qglVertex3fv( tess.vertexPtr[i2].xyz );
				qglVertex3fv( tess.vertexPtr[i2 + tess.numVertexes].xyz );
				qglEnd();
				c_edges++;
			} else {
				c_rejected++;
			}
		}
	}
#endif
}

/*
=================
RB_ShadowTessEnd

triangleFromEdge[ v1 ][ v2 ]


  set triangle from edge( v1, v2, tri )
  if ( facing[ triangleFromEdge[ v1 ][ v2 ] ] && !facing[ triangleFromEdge[ v2 ][ v1 ] ) {
  }
=================
*/
void RB_ShadowTessEnd( void ) {
	int		i;
	int		numTris;
	vec3_t	lightDir;
	GLboolean rgba[4];

	if ( glConfig.stencilBits < 4 ) {
		return;
	}

	VectorCopy( backEnd.currentEntity->lightDir, lightDir );

	// project vertexes away from light direction
	for ( i = 0 ; i < tess.numVertexes ; i++ ) {
		VectorMA( tess.vertexPtr[i].xyz, -512,
			  lightDir, tess.vertexPtr[i+tess.numVertexes].xyz );
	}

	// decide which triangles face the light
	numEdgeDefs = ri.Hunk_AllocateTempMemory( sizeof( int ) * tess.numVertexes );
	edgeDefs = ri.Hunk_AllocateTempMemory( sizeof( edgeDef_t ) * tess.numVertexes * MAX_EDGE_DEFS );
	facing = ri.Hunk_AllocateTempMemory( sizeof( int ) * tess.numIndexes / 3 );
	Com_Memset( numEdgeDefs, 0, sizeof(int) * tess.numVertexes );

	numTris = tess.numIndexes / 3;
	for ( i = 0 ; i < numTris ; i++ ) {
		GLushort	i1, i2, i3;
		vec3_t	d1, d2, normal;
		vec4_t	*v1, *v2, *v3;
		float	d;
		
		if ( tess.indexInc == sizeof( GLuint ) ) {
			GLuint *indexPtr32 = (GLuint *)tess.indexPtr;
			i1 = indexPtr32[ i*3 + 0 ];
			i2 = indexPtr32[ i*3 + 1 ];
			i3 = indexPtr32[ i*3 + 2 ];
		} else {
			i1 = tess.indexPtr[ i*3 + 0 ];
			i2 = tess.indexPtr[ i*3 + 1 ];
			i3 = tess.indexPtr[ i*3 + 2 ];
		}
		
		v1 = &tess.vertexPtr[i1].xyz;
		v2 = &tess.vertexPtr[i2].xyz;
		v3 = &tess.vertexPtr[i3].xyz;

		VectorSubtract( *v2, *v1, d1 );
		VectorSubtract( *v3, *v1, d2 );
		CrossProduct( d1, d2, normal );

		d = DotProduct( normal, lightDir );
		if ( d > 0 ) {
			facing[ i ] = 1;
		} else {
			facing[ i ] = 0;
		}

		// create the edges
		R_AddEdgeDef( i1, i2, facing[ i ] );
		R_AddEdgeDef( i2, i3, facing[ i ] );
		R_AddEdgeDef( i3, i1, facing[ i ] );
	}

	// draw the silhouette edges

	GL_Bind( tr.whiteImage );
	qglEnable( GL_CULL_FACE );
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	qglColor3f( 0.2f, 0.2f, 0.2f );

	// don't write to the color buffer
	qglGetBooleanv(GL_COLOR_WRITEMASK, rgba);
	qglColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );

	qglEnable( GL_STENCIL_TEST );
	qglStencilFunc( GL_ALWAYS, 1, 255 );

	// mirrors have the culling order reversed
	if ( backEnd.viewParms.isMirror ) {
		qglCullFace( GL_FRONT );
		qglStencilOp( GL_KEEP, GL_KEEP, GL_INCR );

		R_RenderShadowEdges();

		qglCullFace( GL_BACK );
		qglStencilOp( GL_KEEP, GL_KEEP, GL_DECR );

		R_RenderShadowEdges();
	} else {
		qglCullFace( GL_BACK );
		qglStencilOp( GL_KEEP, GL_KEEP, GL_INCR );

		R_RenderShadowEdges();

		qglCullFace( GL_FRONT );
		qglStencilOp( GL_KEEP, GL_KEEP, GL_DECR );

		R_RenderShadowEdges();
	}


	// reenable writing to the color buffer
	qglColorMask(rgba[0], rgba[1], rgba[2], rgba[3]);

	ri.Hunk_FreeTempMemory( facing );
	ri.Hunk_FreeTempMemory( edgeDefs );
	ri.Hunk_FreeTempMemory( numEdgeDefs );
}


/*
=================
RB_ShadowFinish

Darken everything that is is a shadow volume.
We have to delay this until everything has been shadowed,
because otherwise shadows from different body parts would
overlap and double darken.
=================
*/
void RB_ShadowFinish( void ) {
	if ( r_shadows->integer != 2 ) {
		return;
	}
	if ( glConfig.stencilBits < 4 ) {
		return;
	}
	qglEnable( GL_STENCIL_TEST );
	qglStencilFunc( GL_NOTEQUAL, 0, 255 );

	qglDisable (GL_CLIP_PLANE0);
	qglDisable (GL_CULL_FACE);

	GL_Bind( tr.whiteImage );

    qglLoadIdentity ();

	qglColor3f( 0.6f, 0.6f, 0.6f );
	GL_State( GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO );

//	qglColor3f( 1, 0, 0 );
//	GL_State( GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );

	qglBegin( GL_QUADS );
	qglVertex3f( -100, 100, -10 );
	qglVertex3f( 100, 100, -10 );
	qglVertex3f( 100, -100, -10 );
	qglVertex3f( -100, -100, -10 );
	qglEnd ();

	qglColor4f(1,1,1,1);
	qglDisable( GL_STENCIL_TEST );
}


/*
=================
RB_ProjectionShadowDeform

=================
*/
void RB_ProjectionShadowDeform( void ) {
	int		i;
	float	h;
	vec3_t	ground;
	vec3_t	light;
	float	groundDist;
	float	d;
	vec3_t	lightDir;

	ground[0] = backEnd.or.axis[0][2];
	ground[1] = backEnd.or.axis[1][2];
	ground[2] = backEnd.or.axis[2][2];

	groundDist = backEnd.or.origin[2] - backEnd.currentEntity->e.shadowPlane;

	VectorCopy( backEnd.currentEntity->lightDir, lightDir );
	d = DotProduct( lightDir, ground );
	// don't let the shadows get too long or go negative
	if ( d < 0.5 ) {
		VectorMA( lightDir, (0.5 - d), ground, lightDir );
		d = DotProduct( lightDir, ground );
	}
	d = 1.0 / d;

	light[0] = lightDir[0] * d;
	light[1] = lightDir[1] * d;
	light[2] = lightDir[2] * d;

	for ( i = 0; i < tess.numVertexes; i++ ) {
		h = DotProduct( tess.vertexPtr[i].xyz, ground ) + groundDist;

		tess.vertexPtr[i].xyz[0] -= light[0] * h;
		tess.vertexPtr[i].xyz[1] -= light[1] * h;
		tess.vertexPtr[i].xyz[2] -= light[2] * h;
	}
}
