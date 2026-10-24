//
// g_weaponwheel.cpp -- weapon wheel (as in the Quake / Quake II RT ports for macOS)
//
// Hold the key bound to "+wheel" (R and the middle mouse button by default, if they
// were free), move the mouse towards a weapon and release the key to switch to it;
// releasing near the centre cancels. In single player the game slows down while the
// wheel is open.
//
// cl_wheel: 0 - off, 1 - on with slow motion, 2 - on without slow motion
//
// The weapons are the player's weapon slots (1..9, 0) in their usual order, so the
// wheel follows whatever the game or a mod defines. Weapons not owned yet are drawn
// faintly, owned weapons without ammo are dimmed.
//

#include <algorithm>
#include <chrono>
#include <cmath>

#include "doomstat.h"
#include "d_player.h"
#include "d_event.h"
#include "c_dispatch.h"
#include "c_bind.h"
#include "keydef.h"
#include "c_console.h"
#include "c_cvars.h"
#include "menustate.h"
#include "gamestate.h"
#include "i_interface.h"
#include "i_time.h"
#include "v_draw.h"
#include "v_font.h"
#include "v_video.h"
#include "texturemanager.h"
#include "sbar.h"
#include "a_weapons.h"
#include "actor.h"

CVAR(Int, cl_wheel, 1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Float, cl_wheel_timescale, 0.25f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Float, cl_wheel_sensitivity, 1.f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
// set once R / MOUSE3 got their default "+wheel" bindings (only if they were free)
CVAR(Bool, _cl_wheel_defaultbinds, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

EXTERN_CVAR(Float, i_timescale)
EXTERN_CVAR(Float, m_sensitivity_x)
EXTERN_CVAR(Float, m_sensitivity_y)
EXTERN_CVAR(Bool, invertmouse)
EXTERN_CVAR(Bool, invertmousex)

extern double TimeScale;

namespace
{
	constexpr int    MAX_ENTRIES = 32;
	constexpr double WHEEL_PI    = 3.14159265358979323846;

	struct WheelEntry
	{
		PClassActor *type;
		int          slot;
	};

	struct
	{
		bool       active = false;
		double     cx = 0, cy = 0; // virtual cursor relative to the centre, -1..1
		int        selected = -1;  // index into entries, -1 = none
		double     openTime = 0;
		double     appliedScale = 1.0; // slow motion factor currently applied to the game clock
		WheelEntry entries[MAX_ENTRIES];
		int        count = 0;
	} wheel;

	double RealSeconds()
	{
		using namespace std::chrono;
		return duration< double >( steady_clock::now().time_since_epoch() ).count();
	}

	player_t *LocalPlayer()
	{
		if( consoleplayer < 0 || consoleplayer >= MAXPLAYERS || !playeringame[ consoleplayer ] )
		{
			return nullptr;
		}
		return &players[ consoleplayer ];
	}

	// the weapons of the player's slots: 1..9 then 0, as the number keys
	void CollectEntries( player_t *player )
	{
		wheel.count = 0;
		for( int s = 1; s <= NUM_WEAPON_SLOTS; s++ )
		{
			const int slot = s % NUM_WEAPON_SLOTS;
			for( int i = 0; i < player->weapons.SlotSize( slot ); i++ )
			{
				PClassActor *type = player->weapons.GetWeapon( slot, i );
				if( type == nullptr || wheel.count >= MAX_ENTRIES )
				{
					continue;
				}
				wheel.entries[ wheel.count++ ] = { type, slot };
			}
		}
	}

	AActor *OwnedWeapon( player_t *player, int i )
	{
		if( i < 0 || i >= wheel.count || player->mo == nullptr )
		{
			return nullptr;
		}
		return player->mo->FindInventory( wheel.entries[ i ].type );
	}

	bool HasAmmo( AActor *weapon, FName ammoName, FName useName )
	{
		AActor *ammo = weapon->PointerVar< AActor >( ammoName );
		if( ammo == nullptr )
		{
			return false;
		}
		return ammo->IntVar( NAME_Amount ) >= std::max( 1, weapon->IntVar( useName ) );
	}

	// owned and able to fire (either mode)
	bool Usable( AActor *weapon )
	{
		if( weapon == nullptr )
		{
			return false;
		}
		static const FName nAmmoUse2( "AmmoUse2" );
		const bool needs1 = weapon->PointerVar< AActor >( NAME_Ammo1 ) != nullptr;
		const bool needs2 = weapon->PointerVar< AActor >( NAME_Ammo2 ) != nullptr;
		if( !needs1 && !needs2 )
		{
			return true; // fist, chainsaw
		}
		return ( needs1 && HasAmmo( weapon, NAME_Ammo1, NAME_AmmoUse1 ) ) ||
		       ( needs2 && HasAmmo( weapon, NAME_Ammo2, nAmmoUse2 ) ) || ( !needs1 && needs2 );
	}

	// angle of entry i, clockwise from 12 o'clock (screen y points down)
	double SlotAngle( int i )
	{
		return -WHEEL_PI / 2 + ( 2 * WHEEL_PI ) * i / std::max( 1, wheel.count );
	}

	void UpdateSelection( player_t *player )
	{
		wheel.selected = -1;

		const double len = std::sqrt( wheel.cx * wheel.cx + wheel.cy * wheel.cy );
		if( len < 0.25 )
		{
			return;
		}

		const double ang  = std::atan2( wheel.cy, wheel.cx );
		double       best = 999.0;
		for( int i = 0; i < wheel.count; i++ )
		{
			if( OwnedWeapon( player, i ) == nullptr )
			{
				continue;
			}
			const double d = std::fabs( std::remainder( ang - SlotAngle( i ), 2 * WHEEL_PI ) );
			if( d < best )
			{
				best           = d;
				wheel.selected = i;
			}
		}
	}

	bool CanOpen()
	{
		if( cl_wheel == 0 || gamestate != GS_LEVEL || menuactive != MENU_Off || ConsoleState != c_up ||
		    demoplayback || paused || automapactive )
		{
			return false;
		}
		player_t *player = LocalPlayer();
		return player != nullptr && player->mo != nullptr && player->playerstate == PST_LIVE &&
		       player->mo->health > 0;
	}

	// slow motion only in single player: the whole local game is slowed
	bool SlowmoAllowed()
	{
		return cl_wheel == 1 && !netgame && !multiplayer && !demoplayback;
	}

	void Close( bool select )
	{
		if( !wheel.active )
		{
			return;
		}
		wheel.active = false;

		player_t *player = LocalPlayer();
		if( !select || player == nullptr || wheel.selected < 0 )
		{
			return;
		}
		AActor *weapon = OwnedWeapon( player, wheel.selected );
		if( weapon == nullptr || weapon == player->ReadyWeapon )
		{
			return;
		}
		// the same as pressing its key: goes through the network like any weapon change
		FString cmd;
		cmd.Format( "use \"%s\"", wheel.entries[ wheel.selected ].type->TypeName.GetChars() );
		AddCommandString( cmd.GetChars() );
	}

	void DefaultBinds()
	{
		if( _cl_wheel_defaultbinds )
		{
			return;
		}
		// R (DirectInput scan code, as all of GZDoom's key numbers) and the middle mouse button
		const unsigned keys[] = { 0x13, KEY_MOUSE3 };
		for( unsigned key : keys )
		{
			if( Bindings.GetBinding( key ).IsEmpty() && DoubleBindings.GetBinding( key ).IsEmpty() )
			{
				Bindings.SetBind( key, "+wheel" );
			}
		}
		_cl_wheel_defaultbinds = true;
	}

	void ApplyTimeScale( double factor )
	{
		if( std::fabs( factor - wheel.appliedScale ) < 1e-4 )
		{
			return;
		}
		const double base = i_timescale >= 0.05f ? double( *i_timescale ) : 1.0;

		// as the i_timescale cvar does it: the game clock continues from where it is
		I_FreezeTime( true );
		TimeScale = base * factor;
		I_FreezeTime( false );

		wheel.appliedScale = factor;
	}
}

static FConsoleCommand Cmd_WheelDown( "+wheel", []( FCommandLine &, int ) {
	if( !CanOpen() )
	{
		return;
	}
	CollectEntries( LocalPlayer() );
	if( wheel.count == 0 )
	{
		return;
	}
	wheel.active   = true;
	wheel.cx       = 0;
	wheel.cy       = 0;
	wheel.selected = -1;
	wheel.openTime = RealSeconds();
} );

static FConsoleCommand Cmd_WheelUp( "-wheel", []( FCommandLine &, int ) { Close( true ); } );

// System_DispatchEvent: while the wheel is open, the mouse moves its cursor instead of the view
bool G_WeaponWheelMouseMove( event_t *ev )
{
	if( !wheel.active || ev->type != EV_Mouse )
	{
		return false;
	}

	// back to the mouse's own movement (screen y down), without the look settings
	const double sx = std::fabs( *m_sensitivity_x ) > 1e-3 ? double( *m_sensitivity_x ) : 1.0;
	const double sy = std::fabs( *m_sensitivity_y ) > 1e-3 ? double( *m_sensitivity_y ) : 1.0;
	double       dx = ev->x / sx;
	double       dy = -ev->y / sy;
	if( invertmousex )
	{
		dx = -dx;
	}
	if( invertmouse )
	{
		dy = -dy;
	}

	const double scale = std::clamp( double( *cl_wheel_sensitivity ), 0.05, 20.0 ) * 0.004;
	wheel.cx += dx * scale;
	wheel.cy += dy * scale;

	// keep the cursor on the ring so that direction changes stay responsive
	const double len = std::sqrt( wheel.cx * wheel.cx + wheel.cy * wheel.cy );
	if( len > 1.0 )
	{
		wheel.cx /= len;
		wheel.cy /= len;
	}

	if( player_t *player = LocalPlayer() )
	{
		UpdateSelection( player );
	}
	return true;
}

// D_Display, every frame
void G_WeaponWheelFrame()
{
	// after the config's bindings are in
	if( gamestate == GS_LEVEL )
	{
		DefaultBinds();
	}

	if( wheel.active && !CanOpen() )
	{
		Close( false );
	}

	if( wheel.active )
	{
		// the inventory may change while it's open
		player_t *player = LocalPlayer();
		CollectEntries( player );
		UpdateSelection( player );
	}

	double target = 1.0;
	if( wheel.active && SlowmoAllowed() )
	{
		// ease in over 150 ms of real time
		const double t = std::clamp( ( RealSeconds() - wheel.openTime ) / 0.15, 0.0, 1.0 );
		target         = 1.0 + ( std::clamp( double( *cl_wheel_timescale ), 0.05, 1.0 ) - 1.0 ) * t;
	}
	ApplyTimeScale( target );
}

// D_Display, over the HUD and under the menus / console
void G_WeaponWheelDraw()
{
	player_t *player = LocalPlayer();
	if( !wheel.active || player == nullptr || wheel.count == 0 )
	{
		return;
	}

	const int    w = twod->GetWidth();
	const int    h = twod->GetHeight();
	const double k = std::max( 1.0, std::min( w / 320.0, h / 240.0 ) ); // virtual 320x240 units
	const double cx = w * 0.5, cy = h * 0.5;

	// dim the scene
	Dim( twod, 0, 0.35f, 0, 0, w, h );

	// a bit closer together when a mod has many weapons
	const double radius = 88.0;
	const double bw     = std::min( 52.0, 2 * radius * std::sin( WHEEL_PI / wheel.count ) - 4.0 );
	const double bh     = 30.0;

	FFont *font = SmallFont;

	for( int i = 0; i < wheel.count; i++ )
	{
		const double a  = SlotAngle( i );
		const double sx = cx + std::cos( a ) * radius * k;
		const double sy = cy + std::sin( a ) * radius * k;
		const double x0 = sx - bw * 0.5 * k;
		const double y0 = sy - bh * 0.5 * k;

		AActor    *owned   = OwnedWeapon( player, i );
		const bool usable  = Usable( owned );
		const bool sel     = i == wheel.selected;
		const bool inHand  = owned != nullptr && owned == player->ReadyWeapon;

		// slot background: highlighted when selected, a bit lighter for the weapon in hand
		PalEntry   bg     = sel ? PalEntry( 255, 150, 24, 16 ) : ( inHand ? PalEntry( 255, 90, 90, 90 ) : PalEntry( 255, 0, 0, 0 ) );
		Dim( twod, bg, sel ? 0.6f : 0.55f, int( x0 ), int( y0 ), int( bw * k ), int( bh * k ) );

		const double alpha = owned ? ( usable ? 1.0 : 0.45 ) : 0.12;

		AActor     *def = GetDefaultByType( wheel.entries[ i ].type );
		FTextureID  icon;
		icon.SetIndex( GetInventoryIcon( def, DI_SKIPREADY ) );
		auto *tex = icon.isValid() ? TexMan.GetGameTexture( icon, true ) : nullptr;

		if( tex != nullptr && tex->GetDisplayWidth() > 0 && tex->GetDisplayHeight() > 0 )
		{
			// pickup sprites are wide: fit into the slot, never enlarged
			const double tw = tex->GetDisplayWidth(), th = tex->GetDisplayHeight();
			const double fit = std::min( { ( bw - 6.0 ) / tw, ( bh - 6.0 ) / th, 1.0 } ) * k;
			DrawTexture( twod, tex, sx, sy,
			             DTA_DestWidthF, tw * fit,
			             DTA_DestHeightF, th * fit,
			             DTA_CenterOffset, true,
			             DTA_Alpha, alpha,
			             TAG_DONE );
		}
		else if( owned )
		{
			// no picture (fist): its name
			FString tag = def->GetTag();
			const double fs = std::min( k, ( bw - 4.0 ) * k / std::max( 1, font->StringWidth( tag ) ) );
			DrawText( twod, font, CR_UNTRANSLATED,
			          sx - font->StringWidth( tag ) * fs * 0.5, sy - font->GetHeight() * fs * 0.5, tag.GetChars(),
			          DTA_ScaleX, fs, DTA_ScaleY, fs, DTA_Alpha, alpha, TAG_DONE );
		}

		// the weapon's key, in the slot's corner
		if( owned )
		{
			char num[ 2 ] = { char( '0' + wheel.entries[ i ].slot ), 0 };
			const double ns = k * 0.75;
			DrawText( twod, font, sel ? CR_WHITE : CR_GRAY, x0 + 2 * k, y0 + 2 * k, num,
			          DTA_ScaleX, ns, DTA_ScaleY, ns, TAG_DONE );
		}
	}

	// caption in the centre
	FString label = "Cancel";
	if( wheel.selected >= 0 )
	{
		label = GetDefaultByType( wheel.entries[ wheel.selected ].type )->GetTag();
	}
	DrawText( twod, font, CR_UNTRANSLATED, cx - font->StringWidth( label ) * k * 0.5,
	          cy - font->GetHeight() * k, label.GetChars(), DTA_ScaleX, k, DTA_ScaleY, k, TAG_DONE );

	if( wheel.selected >= 0 && !Usable( OwnedWeapon( player, wheel.selected ) ) )
	{
		const char *note = "no ammo";
		DrawText( twod, font, CR_RED, cx - font->StringWidth( note ) * k * 0.5, cy + 2 * k, note,
		          DTA_ScaleX, k, DTA_ScaleY, k, TAG_DONE );
	}
}
