 /* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "MouseHandler.h"

#include "CommandColors.h"
#include "InputReceiver.h"
#include "GuiHandler.h"
#include "MiniMap.h"
#include "MouseCursor.h"
#include "TooltipConsole.h"
#include "Game/CameraHandler.h"
#include "Game/Camera.h"
#include "Game/Game.h"
#include "Game/GlobalUnsynced.h"
#include "Game/InMapDraw.h"
#include "Game/SelectedUnitsHandler.h"
#include "Game/TraceRay.h"
#include "Game/Camera/CameraController.h"
#include "Game/Players/Player.h"
#include "Game/Players/PlayerHandler.h"
#include "Game/UI/UnitTracker.h"
#include "Lua/LuaInputReceiver.h"
#include "Map/Ground.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/Fonts/glFont.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/GL/RenderBuffers.h"
#include "Rendering/GL/SubState.h"
#include "Rendering/Textures/Bitmap.h"
#include "Sim/Features/Feature.h"
#include "Sim/Units/Unit.h"
#include "Game/UI/Groups/Group.h"
#include "System/Config/ConfigHandler.h"
#include "System/EventHandler.h"
#include "System/Exceptions.h"
#include "System/FastMath.h"
#include "System/SpringMath.h"
#include "System/SafeUtil.h"
#include "System/StringUtil.h"
#include "System/Input/KeyInput.h"
#include "System/Input/MouseInput.h"
#include "Rml/Backends/RmlUi_Backend.h"

#include "System/Misc/TracyDefs.h"

#include <algorithm>

// can't be up there since those contain conflicting definitions
#include <SDL_mouse.h>
#include <SDL_events.h>
#include <SDL_keycode.h>

using namespace GL::State;


CONFIG(bool, HardwareCursor).defaultValue(false).description("Sets hardware mouse cursor rendering. If you have a low framerate, your mouse cursor will seem \"laggy\". Setting hardware cursor will render the mouse cursor separately from spring and the mouse will behave normally. Note, not all GPU drivers support it in fullscreen mode!");
CONFIG(bool, InvertMouse).defaultValue(false);
CONFIG(bool, MouseRelativeModeWarp).defaultValue(true);
CONFIG(bool, MiniMapMouseWheel).defaultValue(false).description("Whether MiniMap responds to MouseWheel events");

CONFIG(float, CrossSize).defaultValue(12.0f);
CONFIG(float, CrossAlpha).defaultValue(0.5f);
CONFIG(float, CrossMoveScale).defaultValue(1.0f);

CONFIG(float, DoubleClickTime).defaultValue(200.0f).description("Double click time in milliseconds.");
CONFIG(float, ScrollWheelSpeed).defaultValue(-25.0f).minimumValue(-255.f).maximumValue(255.f);

CONFIG(float, MouseDragScrollThreshold).defaultValue(0.3f);
CONFIG(int, MouseDragSelectionThreshold).defaultValue(4).description("Distance in pixels which the mouse must be dragged to trigger a selection box.");
CONFIG(int, MouseDragCircleCommandThreshold).defaultValue(4).description("Distance in pixels which the mouse must be dragged to trigger a circular area command.");
CONFIG(int, MouseDragBoxCommandThreshold).defaultValue(16).description("Distance in pixels which the mouse must be dragged to trigger a rectangular area command.");
CONFIG(int, MouseDragFrontCommandThreshold).defaultValue(30).description("Distance in pixels which the mouse must be dragged to trigger a formation front command.");

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CMouseHandler* mouse = nullptr;

static CInputReceiver*& activeReceiver = CInputReceiver::GetActiveReceiverRef();


CMouseHandler::CMouseHandler()
{
	const int2 viewMouseCenter = GetViewMouseCenter();

	lastx = viewMouseCenter.x;
	lasty = viewMouseCenter.y;

	UpdateCursorCameraDir();

#ifndef __APPLE__
	hardwareCursor = configHandler->GetBool("HardwareCursor");
#endif

	crossSize      = configHandler->GetFloat("CrossSize");
	crossAlpha     = configHandler->GetFloat("CrossAlpha");
	crossMoveScale = configHandler->GetFloat("CrossMoveScale") * 0.005f;

	doubleClickTime = configHandler->GetFloat("DoubleClickTime") / 1000.0f;

	ConfigUpdate();

	configHandler->NotifyOnChange(this, {
		"MiniMapMouseWheel",
		"MouseDragScrollThreshold",
		"MouseDragSelectionThreshold",
		"MouseDragBoxCommandThreshold",
		"MouseDragCircleCommandThreshold",
		"MouseDragFrontCommandThreshold",
		"InvertMouse",
		"ScrollWheelSpeed",
	});
}

CMouseHandler::~CMouseHandler()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (hwHideCursor)
		SDL_ShowCursor(SDL_ENABLE);

	configHandler->RemoveObserver(this);
}


void CMouseHandler::InitStatic()
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(mouse == nullptr);
	assert(mouseInput == nullptr);

	mouseInput = IMouseInput::GetInstance(configHandler->GetBool("MouseRelativeModeWarp"));
	mouse = new CMouseHandler();
}

void CMouseHandler::KillStatic()
{
	RECOIL_DETAILED_TRACY_ZONE;
	spring::SafeDelete(mouse);
	IMouseInput::FreeInstance(mouseInput);
}


void CMouseHandler::ReloadCursors()
{
	RECOIL_DETAILED_TRACY_ZONE;
	const CMouseCursor::HotSpot mCenter  = CMouseCursor::Center;
	const CMouseCursor::HotSpot mTopLeft = CMouseCursor::TopLeft;

	activeCursorIdx = -1;

	loadedCursors.clear();
	loadedCursors.reserve(32);
	// null-cursor; always lives at index 0
	loadedCursors.emplace_back();

	cursorCommandMap.clear();
	cursorCommandMap.reserve(32);
	cursorFileMap.clear();
	cursorFileMap.reserve(32);
	activeCursorName.clear();
	queuedCursorName.clear();

	cursorCommandMap["none"] = loadedCursors.size() - 1;
	cursorFileMap["null"] = loadedCursors.size() - 1;

	AssignMouseCursor("",             "cursornormal",     mTopLeft, false);

	AssignMouseCursor("Area attack",  "cursorareaattack", mCenter,  false);
	AssignMouseCursor("Area attack",  "cursorattack",     mCenter,  false); // backup

	AssignMouseCursor("Attack",       "cursorattack",     mCenter,  false);
	AssignMouseCursor("AttackBad",    "cursorattackbad",  mCenter,  false);
	AssignMouseCursor("AttackBad",    "cursorattack",     mCenter,  false); // backup
	AssignMouseCursor("BuildBad",     "cursorbuildbad",   mCenter,  false);
	AssignMouseCursor("BuildGood",    "cursorbuildgood",  mCenter,  false);
	AssignMouseCursor("Capture",      "cursorcapture",    mCenter,  false);
	AssignMouseCursor("Centroid",     "cursorcentroid",   mCenter,  false);

	AssignMouseCursor("DeathWait",    "cursordwatch",     mCenter,  false);
	AssignMouseCursor("DeathWait",    "cursorwait",       mCenter,  false); // backup

	AssignMouseCursor("ManualFire",   "cursormanfire",    mCenter,  false);
	AssignMouseCursor("ManualFire",   "cursordgun",       mCenter,  false); // backup (backward compatibility)
	AssignMouseCursor("ManualFire",   "cursorattack",     mCenter,  false); // backup

	AssignMouseCursor("Fight",        "cursorfight",      mCenter,  false);
	AssignMouseCursor("Fight",        "cursorattack",     mCenter,  false); // backup

	AssignMouseCursor("GatherWait",   "cursorgather",     mCenter,  false);
	AssignMouseCursor("GatherWait",   "cursorwait",       mCenter,  false); // backup

	AssignMouseCursor("Guard",        "cursordefend",     mCenter,  false);
	AssignMouseCursor("Load units",   "cursorpickup",     mCenter,  false);
	AssignMouseCursor("Move",         "cursormove",       mCenter,  false);
	AssignMouseCursor("Patrol",       "cursorpatrol",     mCenter,  false);
	AssignMouseCursor("Reclaim",      "cursorreclamate",  mCenter,  false);
	AssignMouseCursor("Repair",       "cursorrepair",     mCenter,  false);

	AssignMouseCursor("Resurrect",    "cursorrevive",     mCenter,  false);
	AssignMouseCursor("Resurrect",    "cursorrepair",     mCenter,  false); // backup

	AssignMouseCursor("Restore",      "cursorrestore",    mCenter,  false);
	AssignMouseCursor("Restore",      "cursorrepair",     mCenter,  false); // backup

	AssignMouseCursor("SelfD",        "cursorselfd",      mCenter,  false);

	AssignMouseCursor("SquadWait",    "cursornumber",     mCenter,  false);
	AssignMouseCursor("SquadWait",    "cursorwait",       mCenter,  false); // backup

	AssignMouseCursor("TimeWait",     "cursortime",       mCenter,  false);
	AssignMouseCursor("TimeWait",     "cursorwait",       mCenter,  false); // backup

	AssignMouseCursor("Unload units", "cursorunload",     mCenter,  false);
	AssignMouseCursor("Wait",         "cursorwait",       mCenter,  false);

	// the default cursor must exist
	const auto defCursorIt = cursorCommandMap.find("");

	if (defCursorIt == cursorCommandMap.end()) {
		throw content_error(
			"Unable to load default cursor. Check that you have the required\n"
			"content packages installed in your Spring \"base/\" directory.\n");
	}

	activeCursorIdx = defCursorIt->second;
}

void CMouseHandler::WindowEnter()
{
	RECOIL_DETAILED_TRACY_ZONE;
	offscreen = false;
}

void CMouseHandler::WindowLeave()
{
	RECOIL_DETAILED_TRACY_ZONE;
	offscreen = true;

	const int2 viewMouseCenter = GetViewMouseCenter();

	lastx = viewMouseCenter.x;
	lasty = viewMouseCenter.y;
}


/******************************************************************************/

void CMouseHandler::MouseMove(int x, int y, int dx, int dy)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// FIXME: don't update if locked?
	lastx = x;
	// Origin for mousecursor on internal coordinates is top border of view screen
	lasty = y - globalRendering->viewWindowOffsetY;

	UpdateCursorCameraDir();

	// switching to MMB-mode while user is moving mouse can generate
	// a spurious event in the opposite direction (if cursor happens
	// to pass the center pixel) which would cause a camera position
	// jump, so always ignore one SDL_MOUSEMOTION after entering it
	dx *= (1 - ignoreMove);
	dy *= (1 - ignoreMove);

	ignoreMove = false;

	// calculating deltas (scroll{x,y} = last{x,y} - screenCenter.{x,y})
	// is not required when using relative motion mode, can add directly
	scrollx += (dx * hideCursor);
	scrolly += (dy * hideCursor);

	if (locked) {
		camHandler->GetCurrentController().MouseMove(float3(dx, dy, invertMouse ? -1.0f : 1.0f));
		return;
	}

	const int movedPixels = (int)fastmath::sqrt_sse(float(dx*dx + dy*dy));
	buttons[SDL_BUTTON_LEFT ].movement += movedPixels;
	buttons[SDL_BUTTON_RIGHT].movement += movedPixels;

	if (game != nullptr && !game->IsGameOver())
		playerHandler.Player(gu->myPlayerNum)->currentStats.mousePixels += movedPixels;

	/* Only want to give a mouse event to RmlUI if the mouse isn't currently performing a drag.
	 * Otherwise box selections get stuck when the mouse goes over an Rml element.
	 * Flags that ButtonPressed() checks are not set when clicking on Rml element. */
	if (!ButtonPressed() && RmlGui::ProcessMouseMove(x, lasty, dx, dy, activeButtonIdx)) {
		return;
	}

	if (activeReceiver != nullptr)
		activeReceiver->MouseMove(x, lasty, dx, dy, activeButtonIdx);

	if (inMapDrawer != nullptr && inMapDrawer->IsDrawMode())
		inMapDrawer->MouseMove(x, lasty, dx, dy, activeButtonIdx);

	if (buttons[SDL_BUTTON_MIDDLE].pressed && (activeReceiver == nullptr)) {
		camHandler->GetCurrentController().MouseMove(float3(dx, dy, invertMouse ? -1.0f : 1.0f));
		if (!camHandler->GetActiveCamera()->GetMovState()[CCamera::MOVE_STATE_RTT]) {
			unitTracker.Disable();
		}
		return;
	}
}


void CMouseHandler::MousePress(int x, int y, int button)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (button > NUM_BUTTONS)
		return;

	// Origin for mousecursor on internal coordinates is lower border of view screen
	y = y - globalRendering->viewWindowOffsetY;

	if (game != nullptr && !game->IsGameOver())
		playerHandler.Player(gu->myPlayerNum)->currentStats.mouseClicks++;

	if (RmlGui::ProcessMousePress(x, y, button)) {
		return;
	}

	activeButtonIdx = button;
	ButtonPressEvt& bp = buttons[activeButtonIdx];
	bp.chorded  = (buttons[SDL_BUTTON_LEFT].pressed || buttons[SDL_BUTTON_RIGHT].pressed);
	bp.pressed  = true;
	bp.time     = gu->gameTime;
	bp.x        = x;
	bp.y        = y;
	bp.camPos   = camera->GetPos();
	bp.dir      = (dir = GetCursorCameraDir(x, y));
	bp.movement = 0;

	pressedBitMask |= 1 << button;

	if (activeReceiver != nullptr && activeReceiver->MousePress(x, y, button))
		return;

	if (inMapDrawer != nullptr && inMapDrawer->IsDrawMode()) {
		inMapDrawer->MousePress(x, y, button);
		return;
	}

	if (button == SDL_BUTTON_MIDDLE && locked)
		return;

	if (luaInputReceiver->MousePress(x, y, button)) {
		if (activeReceiver == nullptr)
			activeReceiver = luaInputReceiver;
		return;
	}

	if (game != nullptr && !game->hideInterface) {
		for (CInputReceiver* recv: CInputReceiver::GetReceivers()) {
			if (recv != nullptr && recv->MousePress(x, y, button)) {
				if (activeReceiver == nullptr)
					activeReceiver = recv;

				return;
			}
		}

	}

	auto activeControllerReceiver = (activeController == nullptr) ? nullptr : activeController->GetInputReceiver();
	if (button >= ACTION_BUTTON_MIN && activeControllerReceiver && activeControllerReceiver->MousePress(x, y, button)) {
		activeReceiver = activeControllerReceiver;
		return;
	}

	if (game != nullptr && !game->hideInterface) {
		// skip guihandler in this case
		return;
	}

	if (guihandler != nullptr && guihandler->MousePress(x, y, button)) {
		if (activeReceiver == nullptr)
			activeReceiver = guihandler; // for default (rmb) commands
	}
}

/**
 * GetSelectionBoxVertices
 *  returns the vertices of the SelectionBox in (cam->right, cam->up)-space
 */
bool CMouseHandler::GetSelectionBoxVertices(float3& bl, float3& br, float3& tl, float3& tr) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (activeReceiver != nullptr)
		return false;

	if (RmlGui::IsMouseInteractingWith()) {
		return false;
	}

	if (gu->fpsMode)
		return false;

	if (inMapDrawer != nullptr && inMapDrawer->IsDrawMode())
		return false;

	const ButtonPressEvt& bp = buttons[SDL_BUTTON_LEFT];

	if (!bp.pressed)
		return false;
	if (bp.chorded)
		return false;
	if (bp.movement <= dragSelectionThreshold)
		return false;

	float2 topRight, bttmLeft;

	GetSelectionBoxCoeff(bp.camPos, bp.dir, camera->GetPos(), dir, topRight, bttmLeft);

	// do not let the rectangle verts be clipped
	const float dirScale = camera->GetNearPlaneDist() * 2.0f;

	const float3 xmin   = camera->GetRight() * bttmLeft.x;
	const float3 xmax   = camera->GetRight() * topRight.x;
	const float3 ymin   = camera->GetUp()    * bttmLeft.y;
	const float3 ymax   = camera->GetUp()    * topRight.y;

	bl = camera->GetPos() + (xmin + ymin + camera->GetForward()) * dirScale;
	tr = camera->GetPos() + (xmax + ymax + camera->GetForward()) * dirScale;
	br = camera->GetPos() + (xmax + ymin + camera->GetForward()) * dirScale;
	tl = camera->GetPos() + (xmin + ymax + camera->GetForward()) * dirScale;

	return true;
}
/**
 * GetSelectionBoxCoeff
 *  returns the topright & bottomleft corner positions of the SelectionBox in (cam->right, cam->up)-space
 */
void CMouseHandler::GetSelectionBoxCoeff(
	const float3& pos1,
	const float3& dir1,
	const float3& pos2,
	const float3& dir2,
	float2& topright,
	float2& bttmleft
) {
	const float maxDist = camera->GetFarPlaneDist() * 1.4f;

	float pos1Dist = CGround::LineGroundCol(pos1, pos1 + dir1 * maxDist, false);
	float pos2Dist = CGround::LineGroundCol(pos2, pos2 + dir2 * maxDist, false);

	if (pos1Dist < 0.0f)
		pos1Dist = maxDist;
	if (pos2Dist < 0.0f)
		pos2Dist = maxDist;

	const float3 gpos1 = pos1 + dir1 * pos1Dist;
	const float3 gpos2 = pos2 + dir2 * pos2Dist;

	const float3 cdir1 = (gpos1 - camera->GetPos()).ANormalize();
	const float3 cdir2 = (gpos2 - camera->GetPos()).ANormalize();

	float cdir1_fw = cdir1.dot(camera->GetDir());
	float cdir2_fw = cdir2.dot(camera->GetDir());

	// prevent DivByZero
	cdir1_fw = std::max(std::fabs(cdir1_fw), 0.001f) * std::copysign(1.0f, cdir1_fw);
	cdir2_fw = std::max(std::fabs(cdir2_fw), 0.001f) * std::copysign(1.0f, cdir2_fw);

	// one corner of the rectangle
	topright.x = cdir1.dot(camera->GetRight()) / cdir1_fw;
	topright.y = cdir1.dot(camera->GetUp())    / cdir1_fw;

	// opposite corner
	bttmleft.x = cdir2.dot(camera->GetRight()) / cdir2_fw;
	bttmleft.y = cdir2.dot(camera->GetUp())    / cdir2_fw;

	// sort coeff so topright really is the topright corner
	if (topright.x < bttmleft.x) std::swap(topright.x, bttmleft.x);
	if (topright.y < bttmleft.y) std::swap(topright.y, bttmleft.y);
}


void CMouseHandler::MouseRelease(int x, int y, int button)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const CUnit *_lastClicked = lastClicked;
	lastClicked = nullptr;

	if (button > NUM_BUTTONS)
		return;

	// Origin for mousecursor on internal coordinates is lower border of view screen
	y = y - globalRendering->viewWindowOffsetY;

	dir = GetCursorCameraDir(x, y);

	buttons[button].pressed = false;
	pressedBitMask &= ~(1 << button);

	if (inMapDrawer != nullptr && inMapDrawer->IsDrawMode()) {
		inMapDrawer->MouseRelease(x, y, button);
		return;
	}

	if (RmlGui::ProcessMouseRelease(x, y, button)) {
		return;
	}

	if (activeReceiver != nullptr) {
		activeReceiver->MouseRelease(x, y, button);

		if (!buttons[SDL_BUTTON_LEFT].pressed && !buttons[SDL_BUTTON_MIDDLE].pressed && !buttons[SDL_BUTTON_RIGHT].pressed)
			activeReceiver = nullptr;

		return;
	}

	if (button >= ACTION_BUTTON_MIN && activeController != nullptr && activeController->MouseRelease(x, y, button)) {
		return;
	}

	// Switch camera mode on a middle click that wasn't a middle mouse drag scroll.
	// the latter is determined by the time the mouse was held down:
	// switch (dragScrollThreshold)
	//   <= means a camera mode switch
	//    > means a drag scroll
	if (button == SDL_BUTTON_MIDDLE) {
		if (buttons[SDL_BUTTON_MIDDLE].time > (gu->gameTime - dragScrollThreshold))
			ToggleMiddleClickScroll();
		return;
	}

	if (gu->fpsMode)
		return;
	// outside game, neither guiHandler nor quadField exist and TraceRay would crash
	if (guihandler == nullptr)
		return;

	if ((button == SDL_BUTTON_LEFT) && !buttons[button].chorded) {
		ButtonPressEvt& bp = buttons[SDL_BUTTON_LEFT];

		if (!KeyInput::GetKeyModState(KMOD_SHIFT) && !KeyInput::GetKeyModState(KMOD_CTRL) && selectedUnitsHandler.GetBoxSelectionHandledByEngine())
			selectedUnitsHandler.ClearSelected();

		if (bp.movement > dragSelectionThreshold && selectedUnitsHandler.GetBoxSelectionHandledByEngine()) {
			// select box
			float2 topright;
			float2 bttmleft;

			GetSelectionBoxCoeff(bp.camPos, bp.dir, camera->GetPos(), dir, topright, bttmleft);

			// GetSelectionBoxCoeff returns us the corner pos, but we want to do a inview frustum check.
			// To do so we need the frustum planes (= plane normal + plane offset).
			float3 norm1 =  camera->GetUp();
			float3 norm2 = -camera->GetUp();
			float3 norm3 =  camera->GetRight();
			float3 norm4 = -camera->GetRight();

			#define signf(x) ((x > 0.0f) ? 1.0f : -1.0f)
			if (topright.y != 0.0f) norm1 = (camera->GetDir() * signf(-topright.y)) + (camera->GetUp()    / math::fabs(topright.y));
			if (bttmleft.y != 0.0f) norm2 = (camera->GetDir() * signf( bttmleft.y)) - (camera->GetUp()    / math::fabs(bttmleft.y));
			if (topright.x != 0.0f) norm3 = (camera->GetDir() * signf(-topright.x)) + (camera->GetRight() / math::fabs(topright.x));
			if (bttmleft.x != 0.0f) norm4 = (camera->GetDir() * signf( bttmleft.x)) - (camera->GetRight() / math::fabs(bttmleft.x));

			const float4 plane1(norm1, -(norm1.dot(camera->GetPos())));
			const float4 plane2(norm2, -(norm2.dot(camera->GetPos())));
			const float4 plane3(norm3, -(norm3.dot(camera->GetPos())));
			const float4 plane4(norm4, -(norm4.dot(camera->GetPos())));

			selectedUnitsHandler.HandleUnitBoxSelection(plane1, plane2, plane3, plane4);
		} else {
			const CUnit* unit = nullptr;
			const CFeature* feature = nullptr;

			TraceRay::GuiTraceRay(camera->GetPos(), dir, camera->GetFarPlaneDist() * 1.4f, nullptr, unit, feature, false);
			lastClicked = unit;

			const bool selectType = (bp.lastRelease >= (gu->gameTime - doubleClickTime) && unit == _lastClicked);

			selectedUnitsHandler.HandleSingleUnitClickSelection(const_cast<CUnit*>(unit), true, selectType);
		}

		bp.lastRelease = gu->gameTime;
	}
}

bool CMouseHandler::ButtonPressed()
{
	return pressedBitMask > 0;
}

void CMouseHandler::MouseWheel(float delta)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (RmlGui::ProcessMouseWheel(delta)) {
		return;
	}

	if (eventHandler.MouseWheel(delta > 0.0f, delta))
		return;

	if (miniMapMouseWheel && (minimap != nullptr) && minimap->IsInside(mouse->lastx, mouse->lasty)) {
		minimap->MouseWheel(delta > 0.0f, delta);
		return;
	}

	camHandler->GetCurrentController().MouseWheelMove(delta * scrollWheelSpeed);
}


void CMouseHandler::DrawSelectionBox() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	float3 btLeft, btRight, tpLeft, tpRight;

	if (!GetSelectionBoxVertices(btLeft, btRight, tpLeft, tpRight))
		return;

	auto& rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_C>();
	auto& sh = rb.GetShader();

	rb.AddVertices({
		{btLeft , cmdColors.mouseBox},
		{btRight, cmdColors.mouseBox},
		{tpRight, cmdColors.mouseBox},
		{tpLeft , cmdColors.mouseBox},
	});

	auto state = GL::SubState(
		DepthTest(GL_FALSE),
		Blending(GL_TRUE),
		BlendFunc((GLenum)cmdColors.MouseBoxBlendSrc(), (GLenum)cmdColors.MouseBoxBlendDst()),
		LineWidth(cmdColors.MouseBoxLineWidth()));

	sh.Enable();

	rb.DrawArrays(GL_LINE_LOOP);

	sh.Disable();
}

int2 CMouseHandler::GetViewMouseCenter() const
{
	return {
		globalRendering->viewPosX + (globalRendering->viewSizeX >> 1),
		                            (globalRendering->viewSizeY >> 1)
	};
}

float3 CMouseHandler::GetCursorCameraDir(int x, int y) const { return (hideCursor? camera->GetDir() : camera->CalcPixelDir(x, y)); }
float3 CMouseHandler::GetWorldMapPos() const
{
	const float3 cameraPos = camera->GetPos();
	const float3 cursorVec = dir * camera->GetFarPlaneDist() * 1.4f;

	const float dist = CGround::LineGroundCol(cameraPos, cameraPos + cursorVec, false);

	if (dist < 0.0f)
		return -OnesVector;

	return ((cameraPos + dir * dist).cClampInBounds());
}

// CALLINFO:
// LuaUnsyncedRead::GetCurrentTooltip
// CTooltipConsole::Draw --> CMouseHandler::GetCurrentTooltip
std::string CMouseHandler::GetCurrentTooltip() const
{
	if (!offscreen) {
		std::string s;

		if (luaInputReceiver->IsAbove(lastx, lasty)) {
			s = luaInputReceiver->GetTooltip(lastx, lasty);

			if (!s.empty())
				return s;
		}

		for (CInputReceiver* recv: CInputReceiver::GetReceivers()) {
			if (recv == nullptr)
				continue;
			if (!recv->IsAbove(lastx, lasty))
				continue;

			s = recv->GetTooltip(lastx, lasty);

			if (!s.empty())
				return s;
		}
	}

	// outside game, neither guiHandler nor quadField exist and TraceRay would crash
	if (guihandler == nullptr)
		return "";

	const std::string& buildTip = guihandler->GetBuildTooltip();

	if (!buildTip.empty())
		return buildTip;

	const float range = camera->GetFarPlaneDist() * 1.4f;
	float dist = 0.0f;

	const CUnit* unit = nullptr;
	const CFeature* feature = nullptr;

	{
		dist = TraceRay::GuiTraceRay(camera->GetPos(), dir, range, nullptr, unit, feature, true, false, true);

		if (unit    != nullptr) return CTooltipConsole::MakeUnitString(unit);
		if (feature != nullptr) return CTooltipConsole::MakeFeatureString(feature);
	}

	const string selTip = selectedUnitsHandler.GetTooltip();

	if (!selTip.empty())
		return selTip;

	if (dist <= range)
		return CTooltipConsole::MakeGroundString(camera->GetPos() + (dir * dist));

	return "";
}


void CMouseHandler::Update()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// Rml is very polite about asking for changes to the cursor
	// so let's make sure it's not ignored!
	if (RmlGui::IsMouseInteractingWith())
		// if the cursor string is empty, then Rml is cedeing control of it
		if (auto& rmlCursor = RmlGui::GetMouseCursor(); !rmlCursor.empty())
			queuedCursorName = rmlCursor;

	SetCursor(queuedCursorName);

	if (!hideCursor) {
		mouse->UpdateCursorCameraDir();

		return;
	}

	const int2 viewMouseCenter = GetViewMouseCenter();

	// smoothly decay scroll{x,y} to zero s.t the cursor arrows
	// indicate magnitude and direction of mouse movement while
	// MMB-scrolling
	scrollx *= 0.9f;
	scrolly *= 0.9f;
	lastx = viewMouseCenter.x;
	lasty = viewMouseCenter.y;

	UpdateCursorCameraDir();

	if (!globalRendering->active)
		return;

	mouseInput->SetPos(viewMouseCenter);
}


void CMouseHandler::WarpMouse(int x, int y)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (locked)
		return;

	lastx = x + globalRendering->viewPosX;
	lasty = y;

	mouseInput->SetWarpPos({lastx, lasty});
}


void CMouseHandler::ShowMouse()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!hideCursor)
		return;

	hideCursor = false;

	SDL_SetRelativeMouseMode(SDL_FALSE);

	// don't use SDL_ShowCursor here since it would cause flickering with hwCursor
	// (by switching between default cursor and later the real one, e.g. `attack`)
	// instead update state and cursor at the same time
	ToggleHwCursor(hardwareCursor);


}

void CMouseHandler::HideMouse()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (hideCursor)
		return;

	hideCursor = true;
	hwHideCursor = true;

	SDL_ShowCursor(SDL_DISABLE);
	// signal that we are only interested in relative motion events when MMB-scrolling
	// this way the mouse position will never change so it is also unnecessary to call
	// SDL_WarpMouseInWindow and handle the associated wart of filtering motion events
	// technically supersedes SDL_ShowCursor as well
	SDL_SetRelativeMouseMode(SDL_TRUE);

	const int2 viewMouseCenter = GetViewMouseCenter();

	scrollx = 0.0f;
	scrolly = 0.0f;
	lastx = viewMouseCenter.x;
	lasty = viewMouseCenter.y;

	mouseInput->SetWMMouseCursor(nullptr);
	mouseInput->SetPos(viewMouseCenter);
}


void CMouseHandler::ToggleMiddleClickScroll()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (locked) {
		ShowMouse();
	} else {
		HideMouse();
	}

	locked = !locked;
	mmbScroll = !mmbScroll;
	ignoreMove = mmbScroll;
}


void CMouseHandler::ToggleHwCursor(bool enable)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if ((hardwareCursor = enable)) {
		hwHideCursor = true;
	} else {
		mouseInput->SetWMMouseCursor(nullptr);
		SDL_ShowCursor(SDL_DISABLE);
	}

	// force hardware cursor rebinding, otherwise we get a standard b&w cursor
	activeCursorName = "none";
}


/******************************************************************************/

void CMouseHandler::ChangeCursor(const std::string& cmdName, const float scale)
{
	RECOIL_DETAILED_TRACY_ZONE;
	queuedCursorName = cmdName;
	cursorScale = scale;
}


void CMouseHandler::SetCursor(const std::string& cmdName, const bool forceRebind)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if ((activeCursorName == cmdName) && !forceRebind)
		return;

	const auto it = cursorCommandMap.find(activeCursorName = cmdName);

	if (it != cursorCommandMap.end()) {
		activeCursorIdx = it->second;
	} else {
		activeCursorIdx = cursorCommandMap[""];
	}

	if (!hardwareCursor || hideCursor)
		return;

	if ((hwHideCursor = !loadedCursors[activeCursorIdx].IsHWValid())) {
		SDL_ShowCursor(SDL_DISABLE);
		mouseInput->SetWMMouseCursor(nullptr);
	} else {
		loadedCursors[activeCursorIdx].BindHwCursor(); // calls SDL_ShowCursor(SDL_ENABLE);
	}
}


void CMouseHandler::UpdateCursors()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// we update all cursors (for the command queue icons)
	for (const auto& element: cursorFileMap) {
		loadedCursors[element.second].Update();
	}
}


void CMouseHandler::UpdateCursorCameraDir()
{
	RECOIL_DETAILED_TRACY_ZONE;
	dir = GetCursorCameraDir(lastx, lasty);
}


void CMouseHandler::DrawScrollCursor(TypedRenderBuffer<VA_TYPE_C>& rb) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	const float scaleL = math::fabs(std::min(0.0f, scrollx)) * crossMoveScale + 1.0f;
	const float scaleT = math::fabs(std::min(0.0f, scrolly)) * crossMoveScale + 1.0f;
	const float scaleR = math::fabs(std::max(0.0f, scrollx)) * crossMoveScale + 1.0f;
	const float scaleB = math::fabs(std::max(0.0f, scrolly)) * crossMoveScale + 1.0f;

	rb.AddVertex({{ 0.00f * scaleT,  1.00f * scaleT, 0.0f}, {1.0f, 1.0f, 1.0f, crossAlpha}});
	rb.AddVertex({{ 0.33f * scaleT,  0.66f * scaleT, 0.0f}, {1.0f, 1.0f, 1.0f, crossAlpha}});
	rb.AddVertex({{-0.33f * scaleT,  0.66f * scaleT, 0.0f}, {1.0f, 1.0f, 1.0f, crossAlpha}});

	rb.AddVertex({{ 0.00f * scaleB, -1.00f * scaleB, 0.0f}, {1.0f, 1.0f, 1.0f, crossAlpha}});
	rb.AddVertex({{ 0.33f * scaleB, -0.66f * scaleB, 0.0f}, {1.0f, 1.0f, 1.0f, crossAlpha}});
	rb.AddVertex({{-0.33f * scaleB, -0.66f * scaleB, 0.0f}, {1.0f, 1.0f, 1.0f, crossAlpha}});

	rb.AddVertex({{-1.00f * scaleL,  0.00f * scaleL, 0.0f}, {1.0f, 1.0f, 1.0f, crossAlpha}});
	rb.AddVertex({{-0.66f * scaleL,  0.33f * scaleL, 0.0f}, {1.0f, 1.0f, 1.0f, crossAlpha}});
	rb.AddVertex({{-0.66f * scaleL, -0.33f * scaleL, 0.0f}, {1.0f, 1.0f, 1.0f, crossAlpha}});

	rb.AddVertex({{ 1.00f * scaleR,  0.00f * scaleR, 0.0f}, {1.0f, 1.0f, 1.0f, crossAlpha}});
	rb.AddVertex({{ 0.66f * scaleR,  0.33f * scaleR, 0.0f}, {1.0f, 1.0f, 1.0f, crossAlpha}});
	rb.AddVertex({{ 0.66f * scaleR, -0.33f * scaleR, 0.0f}, {1.0f, 1.0f, 1.0f, crossAlpha}});

	rb.AddVertex({{-0.33f * scaleT,  0.66f * scaleT, 0.0f}, {1.0f, 1.0f, 1.0f, crossAlpha}});
	rb.AddVertex({{ 0.33f * scaleT,  0.66f * scaleT, 0.0f}, {1.0f, 1.0f, 1.0f, crossAlpha}});

	rb.AddVertex({{ 0.00f         ,  0.00f         , 0.0f}, {0.2f, 0.2f, 0.2f,       0.0f}});

	rb.AddVertex({{-0.33f * scaleB, -0.66f * scaleB, 0.0f}, {1.0f, 1.0f, 1.0f, crossAlpha}});
	rb.AddVertex({{ 0.33f * scaleB, -0.66f * scaleB, 0.0f}, {1.0f, 1.0f, 1.0f, crossAlpha}});

	rb.AddVertex({{ 0.00f         ,  0.00f,          0.0f}, {0.2f, 0.2f, 0.2f,       0.0f}});

	rb.AddVertex({{-0.66f * scaleL,  0.33f * scaleL, 0.0f}, {1.0f, 1.0f, 1.0f, crossAlpha}});
	rb.AddVertex({{-0.66f * scaleL, -0.33f * scaleL, 0.0f}, {1.0f, 1.0f, 1.0f, crossAlpha}});

	rb.AddVertex({{ 0.00f         ,  0.00f         , 0.0f}, {0.2f, 0.2f, 0.2f,       0.0f}});

	rb.AddVertex({{ 0.66f * scaleR, -0.33f * scaleR, 0.0f}, {1.0f, 1.0f, 1.0f, crossAlpha}});
	rb.AddVertex({{ 0.66f * scaleR,  0.33f * scaleR, 0.0f}, {1.0f, 1.0f, 1.0f, crossAlpha}});

	rb.AddVertex({{ 0.00f         ,  0.00f         , 0.0f}, {0.2f, 0.2f, 0.2f,       0.0f}});

	// center dot
	rb.AddVertex({{-crossSize * 0.03f,  crossSize * 0.03f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.2f * crossAlpha}});
	rb.AddVertex({{-crossSize * 0.03f, -crossSize * 0.03f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.2f * crossAlpha}});
	rb.AddVertex({{ crossSize * 0.03f, -crossSize * 0.03f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.2f * crossAlpha}});

	rb.AddVertex({{ crossSize * 0.03f, -crossSize * 0.03f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.2f * crossAlpha}});
	rb.AddVertex({{ crossSize * 0.03f,  crossSize * 0.03f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.2f * crossAlpha}});
	rb.AddVertex({{-crossSize * 0.03f,  crossSize * 0.03f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.2f * crossAlpha}});

	rb.DrawArrays(GL_TRIANGLES);
}


void CMouseHandler::DrawFPSCursor(TypedRenderBuffer<VA_TYPE_C>& rb) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	constexpr int stepNumHalf = 2;

	const float wingHalf = math::PI * 0.111111f;
	const float step = wingHalf / stepNumHalf;

	for (float angle = 0.0f; angle < math::TWOPI; angle += (math::TWOPI * 0.333333f)) {
		for (int i = -stepNumHalf; i < stepNumHalf; i++) {
			rb.AddVertex({{0.1f * fastmath::sin(angle                 ), 0.1f * fastmath::cos(angle                 ), 0.0f}, {1.0f, 1.0f, 1.0f, 0.5f}});
			rb.AddVertex({{0.8f * fastmath::sin(angle + (i    ) * step), 0.8f * fastmath::cos(angle + (i    ) * step), 0.0f}, {1.0f, 1.0f, 1.0f, 0.5f}});
			rb.AddVertex({{0.8f * fastmath::sin(angle + (i + 1) * step), 0.8f * fastmath::cos(angle + (i + 1) * step), 0.0f}, {1.0f, 1.0f, 1.0f, 0.5f}});
		}
	}

	rb.Submit(GL_TRIANGLES);
}


void CMouseHandler::DrawCursor()
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(activeCursorIdx != -1);

	if (guihandler != nullptr)
		guihandler->DrawCentroidCursor();

	if (locked) {
		// draw procedural cursor if center-locked
		if (crossSize > 0.0f) {
			const float xscale = crossSize * globalRendering->pixelX;
			const float yscale = crossSize * globalRendering->pixelY;

			glPushMatrix();
			glTranslatef(0.5f - globalRendering->pixelX * 0.5f, 0.5f - globalRendering->pixelY * 0.5f, 0.0f);
			glScalef(xscale, yscale, 1.0f);

			auto& rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_C>();
			auto& sh = rb.GetShader();

			sh.Enable();

			if (gu->fpsMode) {
				DrawFPSCursor(rb);
			} else {
				DrawScrollCursor(rb);
			}

			sh.Disable();

			glPopMatrix();
		}

		return;
	}

	if (hideCursor)
		return;

	if (hardwareCursor && loadedCursors[activeCursorIdx].IsHWValid()) {
		loadedCursors[activeCursorIdx].UpdateHwCursor();
		return;
	}

	// draw the 'software' cursor
	if (cursorScale >= 0.0f) {
		loadedCursors[activeCursorIdx].Draw(lastx, lasty, cursorScale);
		return;
	}

	// hovered minimap, show default cursor and draw `special` cursor scaled-down bottom right of the default one
	const size_t normalCursorIndex = cursorFileMap["cursornormal"];

	if (normalCursorIndex == 0) {
		loadedCursors[activeCursorIdx].Draw(lastx, lasty, -cursorScale);
		return;
	}

	CMouseCursor& normalCursor = loadedCursors[normalCursorIndex];
	normalCursor.Draw(lastx, lasty, 1.0f);

	if (activeCursorIdx == normalCursorIndex)
		return;

	loadedCursors[activeCursorIdx].Draw(lastx + normalCursor.GetMaxSizeX(), lasty + normalCursor.GetMaxSizeY(), -cursorScale);
}


bool CMouseHandler::AssignMouseCursor(
	const std::string& cmdName,
	const std::string& fileName,
	CMouseCursor::HotSpot hotSpot,
	bool overwrite
) {
	RECOIL_DETAILED_TRACY_ZONE;
	const auto  cmdIt = cursorCommandMap.find(cmdName);
	const auto fileIt = cursorFileMap.find(fileName);

	const bool haveCmd  = ( cmdIt != cursorCommandMap.end());
	const bool haveFile = (fileIt != cursorFileMap.end());

	// already assigned a cursor for this command
	if (haveCmd && !overwrite)
		return false;

	// cursor is already loaded but not assigned, reuse it
	if (haveFile) {
		cursorCommandMap[cmdName] = fileIt->second;
		return true;
	}

	const size_t numLoadedCursors = loadedCursors.size();
	const size_t commandCursorIdx = haveCmd? cmdIt->second: numLoadedCursors + 1;

	// assign the new cursor and remap indices
	loadedCursors.emplace_back(fileName, hotSpot);

	// a loaded cursor can be invalid, but if so the command will not be mapped to it
	// allows Lua code to replace the cursor by a valid one later and do reassignment
	// cursorCommandMap[cmdName] = (loadedCursors[numLoadedCursors].IsValid())? numLoadedCursors: commandCursorIdx;
	cursorFileMap[fileName] = numLoadedCursors;

	if (loadedCursors[numLoadedCursors].IsValid())
		cursorCommandMap[cmdName] = numLoadedCursors;

	// switch to the null-cursor if our active one is being (re)assigned
	if (activeCursorIdx == commandCursorIdx)
		SetCursor("none", true);

	return (loadedCursors[numLoadedCursors].IsValid());
}

bool CMouseHandler::ReplaceMouseCursor(
	const string& oldName,
	const string& newName,
	CMouseCursor::HotSpot hotSpot
) {
	RECOIL_DETAILED_TRACY_ZONE;
	const auto fileIt = cursorFileMap.find(oldName);

	if (fileIt == cursorFileMap.end())
		return false;

	const auto* loadedCursor = loadedCursors.data() + fileIt->second;

	if (newName == loadedCursor->GetName() && hotSpot == loadedCursor->GetHotSpot()) //same
		return true;

	CMouseCursor newCursor = CMouseCursor(newName, hotSpot);

	// replace here so SetCursor() operates with new CMouseCursor() object
	// hold on the destruction of old CMouseCursor() in this place. Otherwise bad things will happen.
	std::swap(loadedCursors.at(fileIt->second), newCursor);

	// update current cursor if necessary
	if (activeCursorIdx == fileIt->second)
		SetCursor(activeCursorName, true);

	newCursor = {}; //now it's safe to kill old cursor
	return (loadedCursors[activeCursorIdx].IsValid());
}


/******************************************************************************/

void CMouseHandler::ConfigNotify(const std::string& key, const std::string& value)
{
	RECOIL_DETAILED_TRACY_ZONE;
	ConfigUpdate();
}

void CMouseHandler::ConfigUpdate()
{
	RECOIL_DETAILED_TRACY_ZONE;
	dragScrollThreshold = configHandler->GetFloat("MouseDragScrollThreshold");
	dragSelectionThreshold = configHandler->GetInt("MouseDragSelectionThreshold");
	dragBoxCommandThreshold = configHandler->GetInt("MouseDragBoxCommandThreshold");
	dragCircleCommandThreshold = configHandler->GetInt("MouseDragCircleCommandThreshold");
	dragFrontCommandThreshold = configHandler->GetInt("MouseDragFrontCommandThreshold");
	invertMouse = configHandler->GetBool("InvertMouse");
	miniMapMouseWheel = configHandler->GetBool("MiniMapMouseWheel");
	scrollWheelSpeed = configHandler->GetFloat("ScrollWheelSpeed");
}
