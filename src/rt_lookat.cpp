//
// rt_lookat.cpp -- console command: the name of the wall / floor / ceiling texture under the
// crosshair (for picking textures to give a material, e.g. glossier surfaces)
//

#include "doomstat.h"
#include "d_player.h"
#include "c_dispatch.h"
#include "gamestate.h"
#include "p_local.h"
#include "p_trace.h"
#include "p_linetracedata.h"
#include "texturemanager.h"
#include "g_levellocals.h"

CCMD( rt_lookat )
{
    if( gamestate != GS_LEVEL || consoleplayer < 0 || !playeringame[ consoleplayer ] ||
        players[ consoleplayer ].mo == nullptr )
    {
        Printf( "rt_lookat: not in a level\n" );
        return;
    }

    player_t* pl = &players[ consoleplayer ];
    AActor*   mo = pl->mo;

    FLineTraceData t = {};
    P_LineTrace( mo, mo->Angles.Yaw, 8192.0, mo->Angles.Pitch, TRF_THRUACTORS, pl->viewheight, 0, 0, &t );

    const char* what = t.HitType == TRACE_HitWall      ? "wall"
                       : t.HitType == TRACE_HitFloor   ? "floor"
                       : t.HitType == TRACE_HitCeiling ? "ceiling"
                                                       : "nothing";

    const char* name = "";
    if( t.HitTexture.isValid() )
    {
        if( auto* tex = TexMan.GetGameTexture( t.HitTexture ) )
        {
            name = tex->GetName().GetChars();
        }
    }

    Printf( PRINT_HIGH,
            "rt_lookat: %s %s (%s, %.0f units away)\n",
            what,
            name[ 0 ] ? name : "(no texture)",
            primaryLevel ? primaryLevel->MapName.GetChars() : "?",
            t.Distance );
}
