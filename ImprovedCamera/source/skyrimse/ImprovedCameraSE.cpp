/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

// Precompiled Header
#include "PCH.h"

#include "skyrimse/ImprovedCameraSE.h"

#include "cameras/Cameras.h"
#include "skyrimse/EventsSkyrim.h"
#include "skyrimse/Helper.h"
#include "utils/ICMath.h"
#include "utils/Log.h"
#include "utils/PatternScan.h"
#include "utils/Utils.h"

namespace ImprovedCamera {

	using namespace Address::Variable;

	ImprovedCameraSE::ImprovedCameraSE()
	{
		m_pluginConfig = DLLMain::Plugin::Get()->Config();
		SetupCameraData();
	}

	bool ImprovedCameraSE::ProcessInput(const RE::InputEvent* const* a_event)
	{
		auto pluginGraphics = DLLMain::Plugin::Get()->Graphics();

		if (!a_event || !pluginGraphics)
			return false;

		if (!pluginGraphics->m_UI.get()->IsUIDisplayed())
			return false;

		for (auto event = *a_event; event; event = event->next)
		{
			if (const auto charEvent = event->AsCharEvent())
			{
				pluginGraphics->m_UI.get()->AddCharacterEvent(charEvent->keycode);
			}
			else if (const auto buttonEvent = event->AsButtonEvent())
			{
				auto scanCode = buttonEvent->GetIDCode();
				auto virtualKey = MapVirtualKeyEx(scanCode, MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0));
				Utils::CorrectExtendedKeys(scanCode, &virtualKey);

				switch (event->GetDevice())
				{
					case RE::INPUT_DEVICE::kKeyboard:
					{
						// Fix Printscreen's normal behaviour so ENB/ReShade works.
						if (virtualKey == VK_SNAPSHOT && buttonEvent->IsUp())
							return false;

						pluginGraphics->m_UI.get()->AddKeyEvent(virtualKey, buttonEvent->IsPressed());

						if (virtualKey == VK_LSHIFT || virtualKey == VK_RSHIFT ||
							virtualKey == VK_LCONTROL || virtualKey == VK_RCONTROL ||
							virtualKey == VK_LMENU || virtualKey == VK_RMENU)
						{
							pluginGraphics->m_UI.get()->AddKeyModEvent(virtualKey, buttonEvent->IsPressed());
						}
						break;
					}
					case RE::INPUT_DEVICE::kMouse:
					{
						if (scanCode == 8 || scanCode == 9)  // Mouse Wheel: 8 is UP, 9 is DOWN
						{
							pluginGraphics->m_UI.get()->AddMouseWheelEvent(0.0f, buttonEvent->Value() * (scanCode == 8 ? 1.0f : -1.0f));
						}
						else
						{
							// Only handle 5 mouse buttons
							if (scanCode > 4)
								scanCode = 4;

							pluginGraphics->m_UI.get()->AddMouseButtonEvent(scanCode, buttonEvent->IsPressed());
						}
						break;
					}
				}
			}
		}
		return true;
	}

	void ImprovedCameraSE::UpdateSwitchPOV()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto camera = RE::PlayerCamera::GetSingleton();
		std::uint32_t cameraID = camera->currentState->id;

		switch (cameraID)
		{
			case RE::CameraStates::kFirstPerson:
			{
				// MapMenu detection - Do nothing for now.
				if (m_PreviousCameraID == RE::CameraStates::kTween)
				{}
				// Tween detection - Do nothing for now.
				if (m_CurrentCameraID == RE::CameraStates::kTween)
				{}
				// RaceSex Menu detection
				bool raceMenu = RE::UI::GetSingleton()->IsMenuOpen("RaceSex Menu");
				if (raceMenu)
					camera->worldFOV = *fDefaultWorldFOV;

				m_LastStateID = 0;
				m_IsFirstPerson = true;
				m_IsThirdPersonForced = false;
				break;
			}
			case RE::CameraStates::kThirdPerson:
			{
				auto thirdpersonState = (RE::ThirdPersonState*)camera->currentState.get();
				if (!thirdpersonState->IsInputEventHandlingEnabled() || player->IsInKillMove())
				{
					m_IsFirstPerson = true;
				}
				break;
			}
			// Following 3 only trigger if previous cameraID was RE::CameraStates::kFirstPerson.
			case RE::CameraStates::kMount:
			{
				auto horseCameraState = (RE::HorseCameraState*)camera->currentState.get();
				horseCameraState->targetZoomOffset = *fMinCurrentZoom;
				break;
			}
			case RE::CameraStates::kDragon:
			{
				auto dragonCameraState = (RE::ThirdPersonState*)camera->currentState.get();
				dragonCameraState->targetZoomOffset = *fMinCurrentZoom;
				break;
			}
			// Triggers when wood chopping or mining.
			case RE::CameraStates::kFurniture:
			{
				break;
			}
		}
		// Fixes Main Menu crash if you decide to click on ResetState.
		//    Note: Heavy load order will come inside this function when the game isn't fully loaded!
		auto thirdperson3D = RE::PlayerCharacter::GetSingleton()->Get3D(0);
		if (!thirdperson3D)
			return;

		auto thirdpersonNode = thirdperson3D->AsNode();

		if (cameraID == RE::CameraStates::kFirstPerson)
		{
			bool consoleMenu = RE::UI::GetSingleton()->IsMenuOpen("Console");

			if (!m_pluginConfig->General().bEnableBody || (consoleMenu && !m_pluginConfig->General().bEnableBodyConsole))
			{
				thirdpersonNode->GetFlags().set(RE::NiAVObject::Flag::kHidden);
				return;
			}
		}
		if (cameraID != RE::CameraStates::kFirstPerson)
			DisplayShadows(true);

		thirdpersonNode->GetFlags().reset(RE::NiAVObject::Flag::kHidden);
	}

	void ImprovedCameraSE::UpdateCamera(std::uint8_t currentID, std::uint8_t previousID, float deltaTime)
	{
#ifdef _DEBUG
		if (m_pluginConfig->Logging().bCameraDelta)
			LOG_INFO("DeltaTime: {0:.4f}", deltaTime);
#endif
		m_DeltaTime = deltaTime;
		m_CurrentCameraID = currentID;
		m_PreviousCameraID = previousID;
		// Stop processing on Tween/MapMenu
		bool mapMenu = RE::UI::GetSingleton()->IsMenuOpen("MapMenu");
		if (m_CurrentCameraID == RE::CameraStates::kTween || mapMenu)
			return;

		auto player = RE::PlayerCharacter::GetSingleton();
		auto playerCamera = RE::PlayerCamera::GetSingleton();

		auto actor = playerCamera->cameraTarget.get().get();

		if (!actor->IsPlayerRef() && m_CurrentCameraID != RE::CameraStates::kMount)
		{
			if (currentID == RE::CameraStates::kFirstPerson)
				playerCamera->ForceThirdPerson();

			return;
		}
		for (Interface::ICamera* camera : m_Camera)
		{
			if (camera->OnUpdate(currentID, previousID))
			{
				// Forces the camera back to first person if exiting the dragon from first person
				if (camera->GetID() == RE::CameraStates::kThirdPerson && previousID == RE::CameraStates::kPCTransition &&
					m_CameraEventID == CameraEvent::kDragonTransition && m_IsFirstPerson)
				{
					playerCamera->ForceFirstPerson();
					return;
				}
				// Animation camera is harder to detect what is happening, just use previous CameraEventID
				if (camera->GetID() != RE::CameraStates::kAnimated)
					m_CameraEventID = camera->GetEventID();
#if _DEBUG
				LOG_INFO("UpdateCamera:\t\tCamera: {} ({}) - StateID: {} - CameraEventID: {}", camera->GetName().c_str(), camera->GetID(), camera->GetStateID(), m_CameraEventID);
#endif
			}
			if (camera->HasControl())
			{
				m_ICamera = camera;
			}
		}
		// m_ICamera should never be null
		if (!m_ICamera)
			return;

		//UpdateNearDistance(playerCamera);
		UpdateFOV(playerCamera);

		// Check FirstPerson camera
		if (currentID == RE::CameraStates::kFirstPerson)
		{
			m_IsFirstPerson = true;
			m_IsFakeCamera = false;
			if (m_ICamera->GetStateID() == CameraFirstPerson::State::kFishingIdle)
				m_LastStateID = CameraFirstPerson::State::kFishingIdle;

			// We force cartride to use the alternative camera.
			if (m_ICamera->GetStateID() >= CameraFirstPerson::State::kCartRideEnter)
				m_IsFakeCamera = true;
			// Correct furniture idles which should never play in first person
			if (Helper::CorrectFurnitureIdle())
			{
				playerCamera->ForceThirdPerson();
				return;
			}
			// Force Touring Carriages/ParaGliding back to third person
			if (Events::Observer::Get()->IsCurrentAnimation("CartPrisonerCSway") || Events::Observer::Get()->IsCurrentAnimation("ParaGlide"))
			{
				playerCamera->ForceThirdPerson();
				return;
			}
		}
		// Fixes issue with player following Vanity Camera
		else if (currentID == RE::CameraStates::kAutoVanity)
		{
			m_IsFirstPerson = false;
			m_IsFakeCamera = false;
		}
		// Fixes flyback issue with Sawmill failing to load a log
		//    Note: Game engine bug, you can get stuck and just need to click the logs until it frees itself.
		else if (currentID == RE::CameraStates::kFurniture && m_ICamera->GetID() == RE::CameraStates::kFirstPerson)
		{
			m_IsFakeCamera = true;
		}
		// Fixes Scripted/Animation to force back into first person once finished and also Horse/Dragon if were in fake first person.
		else if (m_ICamera->GetID() == RE::CameraStates::kThirdPerson && (m_ICamera->GetStateID() >= CameraThirdPerson::State::kEnter && m_ICamera->GetStateID() <= CameraThirdPerson::State::kWeaponDrawnIdle) &&
			(m_LastStateID == CameraThirdPerson::State::kScriptedIdle || m_LastStateID == CameraThirdPerson::State::kAnimationIdle ||
			m_PreviousCameraID == RE::CameraStates::kMount || m_PreviousCameraID == RE::CameraStates::kDragon) && m_IsThirdPersonForced)
		{
			if (!Helper::IsInteracting(player))
			{
				m_LastStateID = 0;
				m_IsThirdPersonForced = false;
				playerCamera->ForceFirstPerson();
				return;
			}
		}
		// Check all other cameras
		else if (currentID > RE::CameraStates::kFirstPerson && m_ICamera->GetID() > RE::CameraStates::kFirstPerson)
		{
			m_IsFakeCamera = true;

			// Fixes issue where camera does not reset m_IsFirstPerson.
			if (m_ICamera->GetID() == RE::CameraStates::kThirdPerson && m_ICamera->GetStateID() == CameraThirdPerson::State::kIdle &&
				m_LastStateID && m_LastStateID != CameraThirdPerson::State::kScriptedIdle && m_LastStateID != CameraThirdPerson::State::kAnimationIdle && m_IsFirstPerson)
			{
				if (m_LastStateID)
					playerCamera->ForceFirstPerson();

				m_LastStateID = 0;
				m_IsFirstPerson = false;
			}
			// Ignore DeathCinematic for first person bows and magic.
			if (m_ICamera->GetID() == RE::CameraStates::kVATS && previousID == RE::CameraStates::kFirstPerson &&
				(Helper::IsAiming(player) || Helper::IsAiming(player, true) || Helper::IsCastingMagic(player)))
			{
				m_IsFirstPerson = false;
			}
			// Ignore unknown state, happens due to camera's switching and doesn't know the real event was disabled.
			if (m_ICamera->GetID() == RE::CameraStates::kPCTransition && m_ICamera->GetStateID() == CameraTransition::State::kPlaying ||
				currentID == RE::CameraStates::kAnimated && m_CameraEventID == CameraEvent::kScripted)
			{
				m_IsFirstPerson = false;
			}
			// Ignore normal Third Person
			if (m_ICamera->GetID() == RE::CameraStates::kThirdPerson &&
				m_ICamera->GetStateID() > CameraThirdPerson::State::kExit && m_ICamera->GetStateID() < CameraThirdPerson::State::kCraftingEnter && !m_IsThirdPersonForced)
			{
				m_IsFirstPerson = false;
				m_IsFakeCamera = false;
				ReleaseAPIs();
			}
			// Ignore interacting with furniture
			if (m_ICamera->GetID() == RE::CameraStates::kFurniture && m_ICamera->GetStateID() == CameraFurniture::State::kIdle)
				m_IsFakeCamera = false;

			// Control the mouse wheel zoom in/out for Fake First Person
			if (m_ICamera->GetID() == RE::CameraStates::kThirdPerson && m_ICamera->GetStateID() >= CameraThirdPerson::State::kScriptedEnter ||
				m_ICamera->GetID() == RE::CameraStates::kMount && m_ICamera->GetStateID() >= CameraHorse::State::kMounted && m_ICamera->GetStateID() <= CameraHorse::State::kWeaponDrawnIdle ||
				m_ICamera->GetID() == RE::CameraStates::kDragon && m_ICamera->GetStateID() >= CameraDragon::State::kMounted && m_ICamera->GetStateID() <= CameraDragon::State::kRiding)
			{
				if (m_IsFirstPerson)
				{
					auto thirdpersonState = (RE::ThirdPersonState*)playerCamera->currentState.get();
					float zoom = *fMinCurrentZoom + 0.03f;
					m_LastStateID = m_ICamera->GetStateID();
					m_IsFakeCamera = false;
					m_IsThirdPersonForced = false;

					if (thirdpersonState->targetZoomOffset <= zoom)
					{
						// Need to set the following to block entering first person.
						thirdpersonState->currentZoomOffset = zoom;
						thirdpersonState->targetZoomOffset = -0.2f;  // Default savedZoomOffset: -0.2
						m_IsFakeCamera = true;
						m_IsThirdPersonForced = true;

						if (m_SmoothCamAPI)
						{
							auto smoothCamResult = m_SmoothCamAPI->RequestCameraControl(m_ICHandle);
							if (smoothCamResult == SmoothCamAPI::APIResult::OK)
							{}  // Just to stop compiler moaning

							if (!m_SmoothCamSnapshot)
								m_SmoothCamSnapshot = true;
						}
						if (m_TDMAPI)
						{
							auto tdmHeadTrackingResult = m_TDMAPI->RequestDisableHeadtracking(m_ICHandle);
							if (tdmHeadTrackingResult == TDM_API::APIResult::OK)
							{}  // Just to stop compiler moaning

							if (!m_TDMSnapshot)
							{
								m_TDMSnapshot = true;
								m_directionalMovementSheathedOriginal = *m_directionalMovementSheathed;
								m_directionalMovementDrawnOriginal = *m_directionalMovementDrawn;
							}
							*m_directionalMovementSheathed = TDM_API::DirectionalMovementMode::kDisabled;
							*m_directionalMovementDrawn = TDM_API::DirectionalMovementMode::kDisabled;
						}
					}
					else
					{
						ReleaseAPIs();
					}
				}
			}
			// Reset m_LastStateID upon player death
			if (m_ICamera->GetID() == RE::CameraStates::kBleedout && m_ICamera->GetStateID() == CameraRagdoll::State::kPlayingDeath && m_LastStateID)
				m_LastStateID = 0;

			// Don't force first person mode
			if (!m_IsFirstPerson)
				m_IsFakeCamera = false;

			// Do we want to process this event?
			if (!*m_ICamera->GetData().EventActive)
			{
				m_IsFirstPerson = false;
				m_IsFakeCamera = false;
			}
		}
		// Good to go start translating the camera
		if (m_CurrentCameraID == RE::CameraStates::kFirstPerson || m_IsFakeCamera)
			TranslateCamera();
	}

	void ImprovedCameraSE::UpdateFirstPerson()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto firstpersonNode = player->Get3D(1)->AsNode();
		auto thirdpersonNode = player->Get3D(0)->AsNode();
		auto playerCamera = RE::PlayerCamera::GetSingleton();
		auto cameraID = playerCamera->currentState->id;
		bool mapMenu = RE::UI::GetSingleton()->IsMenuOpen("MapMenu");

		auto actor = playerCamera->cameraTarget.get().get();

		if (!actor->IsPlayerRef() && m_CurrentCameraID != RE::CameraStates::kMount)
			return;

		// Don't process on AutoVanity or MapMenu
		if (cameraID == RE::CameraStates::kAutoVanity || mapMenu)
			return;
		// Don't process on TFC
		static bool resetFOV;
		if (cameraID == RE::CameraStates::kFree)
		{
			if (!resetFOV)
			{
				resetFOV = true;
				m_IsFakeCamera = false;
				UpdateSkeleton(true);
				ResetPlayerNodes();
				playerCamera->worldFOV = *fDefaultWorldFOV;
			}
			return;
		}
		resetFOV = false;

		m_thirdpersonLocalTranslate = thirdpersonNode->local.translate;

		if (m_iRagdollFrame)
		{
			m_iRagdollFrame++;

			if (m_iRagdollFrame > 6)
				m_iRagdollFrame = 0;
		}

		// Reset head states when m_IsFakeCamera is switched off.
		auto thirdpersonState = (RE::ThirdPersonState*)RE::PlayerCamera::GetSingleton()->currentState.get();
		if (cameraID != RE::CameraStates::kFirstPerson && thirdpersonState->targetZoomOffset > *fMinCurrentZoom)
		{
			if (!m_ICamera)
				return;

			bool isWerewolf = m_ICamera->GetID() == RE::CameraStates::kThirdPerson && m_ICamera->GetStateID() == CameraThirdPerson::State::kWerewolfIdle;
			auto headNode = Helper::GetHeadNode(thirdpersonNode);

			if (isWerewolf)
				headNode = Helper::FindNode(thirdpersonNode, "NPC Neck [Neck]");

			if (headNode && headNode->local.scale < 0.002f)
			{
				UpdateSkeleton(true);
				Helper::UpdateNode(thirdpersonNode);
			}
		}

		if (cameraID == RE::CameraStates::kFirstPerson || m_CurrentCameraID == RE::CameraStates::kTween && m_PreviousCameraID == RE::CameraStates::kFirstPerson || m_IsFakeCamera)
		{
			// Update player scale
			if (m_pluginConfig->General().bAdjustPlayerScale)
			{
				if (firstpersonNode->local.scale != thirdpersonNode->local.scale)
					firstpersonNode->local.scale = thirdpersonNode->local.scale;
			}

			// Update arms
			auto leftarmNode = Helper::FindNode(firstpersonNode, "NPC L UpperArm [LUar]");
			auto rightarmNode = Helper::FindNode(firstpersonNode, "NPC R UpperArm [RUar]");
			float armScale = UseThirdPersonArms() ? 0.001f : 1.0f;
			float headRot = 0.0f;

			leftarmNode->local.scale = armScale;
			rightarmNode->local.scale = armScale;

			if (UseThirdPersonLeftArm())
				leftarmNode->local.scale = 0.001f;

			if (UseThirdPersonRightArm())
				rightarmNode->local.scale = 0.001f;

			TranslateThirdPersonModel();

			UpdateSkeleton(false);
			Helper::UpdateNode(thirdpersonNode);
			UpdateSkeleton(true);

			DisplayShadows(m_pluginConfig->General().bEnableShadows);

			if (GetHeadRotation(&headRot) || m_IsFakeCamera)
				TranslateFirstPersonModel();
		}
	}

	bool ImprovedCameraSE::SmoothAnimationTransitions()
	{
		const auto camera = RE::PlayerCamera::GetSingleton();
		const auto controlMap = RE::ControlMap::GetSingleton();

		if (camera->IsInFirstPerson())
		{
			float headRotation = 0.0f;

			if (controlMap->IsMovementControlsEnabled() && GetHeadRotation(&headRotation) && headRotation > 0.0f)
				return true;
			else
				return false;
		}
		return true;
	}

	bool ImprovedCameraSE::UpdateHeadTracking() const
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto camera = RE::PlayerCamera::GetSingleton();

		bool onMount = Helper::IsOnMount(player);
		bool mounted = onMount && Helper::IsInteracting(player);

		// Ignore Furniture
		if (m_CameraEventID == CameraEvent::kFurniture && (camera->IsInFirstPerson() || m_IsFakeCamera))
			return false;
		// Ignore Crafting
		if (m_CameraEventID == CameraEvent::kCrafting && (camera->IsInFirstPerson() || m_IsFakeCamera))
			return false;
		// Ignore Scripted
		if (m_CameraEventID == CameraEvent::kScripted && (camera->IsInFirstPerson() || m_IsFakeCamera))
			return false;
		// Only here for fun
		/*
		if (camera->IsInFirstPerson() && player->AsActorState()->IsWeaponDrawn())
			return false;
		*/
		return (!onMount || mounted);
	}

	bool ImprovedCameraSE::ShaderReferenceEffectFix1(void* arg1, RE::Actor* actor)
	{
		using namespace Address::Function;

		auto player = RE::PlayerCharacter::GetSingleton();
		auto camera = RE::PlayerCamera::GetSingleton();
		auto tfcMode = camera->IsInFreeCameraMode();

		if (actor != player || (!camera->IsInFirstPerson() && !tfcMode))
		{
			return ShaderReferenceEffect_Sub_140584680(arg1);
		}

		bool rtn = false;
		// Only shade the arms if fighting and not using third person arms
		if (player->AsActorState()->IsWeaponDrawn() && !UseThirdPersonArms() && !tfcMode)
		{
			rtn = ShaderReferenceEffect_Sub_140584680(arg1);
		}
		else
		{
			bool saveState = player->GetPlayerRuntimeData().playerFlags.isInThirdPersonMode;
			player->GetPlayerRuntimeData().playerFlags.isInThirdPersonMode = true;
			rtn = ShaderReferenceEffect_Sub_140584680(arg1);
			player->GetPlayerRuntimeData().playerFlags.isInThirdPersonMode = saveState;
		}
		return rtn;
	}

	void ImprovedCameraSE::ShaderReferenceEffectFix2(void* arg1, RE::Actor* actor)
	{
		typedef void (*FuncType_Void_Void)(void*);
		FuncType_Void_Void ShaderReferenceEffect2 = (FuncType_Void_Void)Helper::GetVTableAddress(arg1, 0x1D8);

		auto player = RE::PlayerCharacter::GetSingleton();
		auto camera = RE::PlayerCamera::GetSingleton();
		auto tfcMode = camera->IsInFreeCameraMode();

		if (actor != player || (!camera->IsInFirstPerson() && !tfcMode))
		{
			ShaderReferenceEffect2(arg1);
			return;
		}
		// Only shade the arms if fighting and not using third person arms
		if (player->AsActorState()->IsWeaponDrawn() && !UseThirdPersonArms() && !tfcMode)
		{
			ShaderReferenceEffect2(arg1);
		}
		else
		{
			bool saveState = player->GetPlayerRuntimeData().playerFlags.isInThirdPersonMode;
			player->GetPlayerRuntimeData().playerFlags.isInThirdPersonMode = true;
			ShaderReferenceEffect2(arg1);
			player->GetPlayerRuntimeData().playerFlags.isInThirdPersonMode = saveState;
		}
	}

	void ImprovedCameraSE::ResetPlayerNodes()
	{
		using namespace Address::Function;

		auto player = RE::PlayerCharacter::GetSingleton();

		if (!player)
			return;

		const auto magicEffectNodes = player->GetActorRuntimeData().magicCasters;
		for (auto p = magicEffectNodes; p < magicEffectNodes + 4; p++)
		{
			if (p && *p)
				ResetNodes(*p);
		}
	}

	void ImprovedCameraSE::ForceFirstPerson()
	{
#if _DEBUG
		LOG_INFO("ForceFirstPerson:\tPrevious Camera: {} - CameraEventID: {}", m_ICamera->GetID(), m_CameraEventID);
#endif
		m_IsFirstPerson = true;
		m_IsFakeCamera = false;

		// Fixes mods/scripts attempting to break the camera.
		//    9/10 animations do not work in first person.
		auto player = RE::PlayerCharacter::GetSingleton();
		auto camera = RE::PlayerCamera::GetSingleton();
		// Bandaid fix for Triumvirate added
		if (Helper::IsInteracting(player) || strcmp(player->GetRace()->GetName(), "Druid Deer") == 0)
		{
			camera->ForceThirdPerson();
		}
	}

	void ImprovedCameraSE::ForceThirdPerson()
	{
#if _DEBUG
		LOG_INFO("ForceThirdPerson:\tPrevious Camera: {} - CameraEventID: {}", m_ICamera->GetID(), m_CameraEventID);
#endif
		m_IsFirstPerson = false;
		m_IsFakeCamera = false;
		m_IsThirdPersonForced = false;

		if (m_ICamera && m_ICamera->GetID() == RE::CameraState::kFirstPerson)
		{
			bool dialogueMenu = RE::UI::GetSingleton()->IsMenuOpen("Dialogue Menu");

			if (m_LastStateID != CameraFirstPerson::State::kFishingIdle && !dialogueMenu)
			{
				m_IsFirstPerson = true;
				m_IsFakeCamera = true;
				m_IsThirdPersonForced = true;
			}
			m_LastStateID = 0;
		}
	}

	void ImprovedCameraSE::TogglePOV()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		// Bandaid fix for Triumvirate added
		if (!m_ICamera || strcmp(player->GetRace()->GetName(), "Druid Deer") == 0)
			return;

		auto thirdpersonState = (RE::ThirdPersonState*)RE::PlayerCamera::GetSingleton()->currentState.get();
		float minCurrentZoom = *fMinCurrentZoom;

		if (m_ICamera->GetID() == RE::CameraStates::kThirdPerson && m_ICamera->GetStateID() >= CameraThirdPerson::State::kScriptedEnter)
		{
			if (thirdpersonState->targetZoomOffset == minCurrentZoom)
				thirdpersonState->targetZoomOffset = -0.125f;
			else
			{
				m_IsFirstPerson = true;
				thirdpersonState->targetZoomOffset = minCurrentZoom;
			}
		}
		else if (m_ICamera->GetID() == RE::CameraStates::kMount && m_ICamera->GetStateID() >= CameraHorse::State::kRiding && m_ICamera->GetStateID() <= CameraHorse::State::kWeaponDrawnIdle)
		{
			if (thirdpersonState->targetZoomOffset == minCurrentZoom)
				thirdpersonState->targetZoomOffset = -0.125f;
			else
			{
				m_IsFirstPerson = true;
				thirdpersonState->targetZoomOffset = minCurrentZoom;
			}
		}
		else if (m_ICamera->GetID() == RE::CameraStates::kDragon && m_ICamera->GetStateID() == CameraDragon::State::kRiding)
		{
			if (thirdpersonState->targetZoomOffset == minCurrentZoom)
				thirdpersonState->targetZoomOffset = -0.125f;
			else
			{
				m_IsFirstPerson = true;
				thirdpersonState->targetZoomOffset = minCurrentZoom;
			}
		}
	}

	void ImprovedCameraSE::ResetState(bool forced)
	{
		auto ui = RE::UI::GetSingleton();
		bool mainMenu = ui->IsMenuOpen("Main Menu");
		bool loadingMenu = ui->IsMenuOpen("Loading Menu");
		auto player = RE::PlayerCharacter::GetSingleton();

		if (!player)
			return;

		if (forced && !mainMenu && !loadingMenu && !Helper::IsBeastMode() && !player->IsOnMount())
		{
			auto camera = RE::PlayerCamera::GetSingleton();
			auto controlMap = RE::ControlMap::GetSingleton();
			auto playerControls = RE::PlayerControls::GetSingleton();
			auto firstperson3D = RE::PlayerCharacter::GetSingleton()->Get3D(1);
			auto thirdpersonState = (RE::ThirdPersonState*)camera->cameraStates[RE::CameraState::kThirdPerson].get();
			// Fixes interaction with crafting/furniture
			//    Certain objects are used from firstperson node, this should not be hidden.
			if (camera->IsInFirstPerson() && firstperson3D)
			{
				auto firstpersonNode = firstperson3D->AsNode();
				firstpersonNode->GetFlags().reset(RE::NiAVObject::Flag::kHidden);
			}
			// Fixes inputEventHandling, crafting usually disables this.
			thirdpersonState->inputEventHandlingEnabled = true;
			// Fixes common enabledControls issues.
			controlMap->GetRuntimeData().enabledControls.set(RE::UserEvents::USER_EVENT_FLAG::kMovement,
				RE::UserEvents::USER_EVENT_FLAG::kLooking,
				RE::UserEvents::USER_EVENT_FLAG::kPOVSwitch,
				RE::UserEvents::USER_EVENT_FLAG::kFighting);
			// Force to third person to correct any other issues.
			camera->ForceThirdPerson();
			// Fixes being stuck in Scripted state
			playerControls->data.povScriptMode = false;
			player->NotifyAnimationGraph("IdleForceDefaultState");
			// Push the actor away for good measure!
			RE::NiPoint3 position = player->GetPosition();
			player->GetActorRuntimeData().currentProcess->KnockExplosion(player, position, 0.1f);
		}
		m_IsThirdPersonForced = false;
		m_IsFirstPerson = false;
		m_IsFakeCamera = false;

		UpdateSwitchPOV();
	}

	void ImprovedCameraSE::Ragdoll(RE::Actor* actor)
	{
		auto player = RE::PlayerCharacter::GetSingleton();

		if (actor == player)
		{
			auto thirdpersonNode = player->Get3D(0)->AsNode();
			thirdpersonNode->local.translate = m_thirdpersonLocalTranslate;
			Helper::UpdateNode(thirdpersonNode);
			m_iRagdollFrame = 1;
		}
	}

	bool ImprovedCameraSE::Ragdoll_IsTaskPoolRequired(RE::Actor* actor) const
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		return (actor == player && m_iRagdollFrame < 2);
	}

	void ImprovedCameraSE::Ragdoll_UpdateObjectUpwards(RE::Actor* actor)
	{
		auto player = RE::PlayerCharacter::GetSingleton();

		if (actor == player)
		{
			if (m_iRagdollFrame < 2)
			{
				auto thirdpersonNode = player->Get3D(0)->AsNode();
				thirdpersonNode->local.translate = m_thirdpersonLocalTranslate;
				Helper::UpdateNode(thirdpersonNode);
			}
			m_iRagdollFrame = 0;
		}
	}

	void ImprovedCameraSE::RequestAPIs()
	{
		m_ICHandle = SKSE::GetPluginHandle();

		if (!SmoothCamAPI::RegisterInterfaceLoaderCallback(SKSE::GetMessagingInterface(),
				[](void* interfaceInstance, SmoothCamAPI::InterfaceVersion interfaceVersion) {
					if (interfaceVersion == SmoothCamAPI::InterfaceVersion::V3)
						m_SmoothCamAPI = reinterpret_cast<SmoothCamAPI::IVSmoothCam3*>(interfaceInstance);
				}))
		{}

		if (!SmoothCamAPI::RequestInterface(SKSE::GetMessagingInterface(), SmoothCamAPI::InterfaceVersion::V3))
		{}

		m_TDMAPI = reinterpret_cast<TDM_API::IVTDM2*>(TDM_API::RequestPluginAPI(TDM_API::InterfaceVersion::V2));
		if (m_TDMAPI)
		{
			auto directionalMovementSheathed = Utils::FindPattern("TrueDirectionalMovement.dll", "02 00 00 00 02 00 00 00 02 00 00 00");
			auto directionalMovementDrawn = directionalMovementSheathed + 0x4;
			m_directionalMovementSheathed = (std::uint32_t*)directionalMovementSheathed;
			m_directionalMovementDrawn = (std::uint32_t*)directionalMovementDrawn;
		}
	}

	void ImprovedCameraSE::DetectMods()
	{
		auto dataHandler = RE::TESDataHandler::GetSingleton();
		if (dataHandler)
		{
			// ArcheryGameplayOverhaul
			auto agoMod = const_cast<RE::TESFile*>(dataHandler->LookupModByName("DSerArcheryGameplayOverhaul.esp"));
			if (agoMod)
				Helper::archeryGameplayOverhaul = true;
		}
	}
	////////////////////////////////////////////////////////////////
	bool ImprovedCameraSE::UseThirdPersonArms()
	{
		using namespace Address::Function;

		auto player = RE::PlayerCharacter::GetSingleton();
		auto playerState = player->AsActorState();
		bool weaponDrawn = playerState->IsWeaponDrawn();
		bool torchEquipped = GetEquippedItemTypeID(player) == RE::EQUIPPED_ITEMTYPE_ID::kTorch ? true : false;

		if (Events::Observer::Get()->IsCurrentAnimation("IdleReadElderScroll") || Events::Observer::Get()->IsCurrentAnimation("DrinkPotion") ||
			Events::Observer::Get()->IsCurrentAnimation("TakeItem"))// || Events::Observer::Get()->IsCurrentAnimation("ParaGlide"))
			return false;

		if (m_CurrentCameraID == RE::CameraState::kFirstPerson && (playerState->IsSprinting() || player->IsInMidair() || playerState->IsSwimming()) &&
			!weaponDrawn && !Helper::IsBeastMode() && !m_pluginConfig->General().bEnableThirdPersonArms && m_pluginConfig->Fixes().bFirstPersonOverhaul &&
			!m_pluginConfig->Fixes().bOverrideVanillaArmsOnMovement)
		{
			return false;
		}
		if ((m_CurrentCameraID != RE::CameraState::kFirstPerson && !(m_CurrentCameraID == RE::CameraStates::kTween && m_PreviousCameraID == RE::CameraStates::kFirstPerson)) ||
			Helper::IsOnMount(player) || (!weaponDrawn && !torchEquipped))
		{
			return true;
		}
		if (Helper::IsSitting(player) && torchEquipped)
			return true;

		if (Helper::IsAiming(player))
			return m_pluginConfig->General().bEnableThirdPersonBowAim;

		if (Helper::IsRangedWeaponEquipped(player))
			return m_pluginConfig->General().bEnableThirdPersonBow;

		if (Helper::IsAiming(player, true))
			return m_pluginConfig->General().bEnableThirdPersonCrossbowAim;

		if (Helper::IsRangedWeaponEquipped(player, true))
			return m_pluginConfig->General().bEnableThirdPersonCrossbow;

		return m_pluginConfig->General().bEnableThirdPersonArms;
	}

	bool ImprovedCameraSE::UseThirdPersonLeftArm()
	{
		using namespace Address::Function;

		auto player = RE::PlayerCharacter::GetSingleton();
		auto playerState = player->AsActorState();
		bool shieldEquipped = GetEquippedItemTypeID(player) == RE::EQUIPPED_ITEMTYPE_ID::kShield ? true : false;
		bool torchEquipped = GetEquippedItemTypeID(player) == RE::EQUIPPED_ITEMTYPE_ID::kTorch ? true : false;
		bool playerBlocking = player->IsBlocking();

		// CFPAO/Animated Interactions fix
		if (((playerState->IsSprinting() || player->IsInMidair()) && !playerState->IsWeaponDrawn() && !Helper::IsBeastMode() &&
			m_pluginConfig->Fixes().bFirstPersonOverhaul && !m_pluginConfig->Fixes().bOverrideVanillaArmsOnMovement) ||
			Events::Observer::Get()->IsCurrentAnimation("TakeItem"))
		{
			return false;
		}
		// Potion Drinking fix
		if (Events::Observer::Get()->IsCurrentAnimation("DrinkPotion"))
			return true;

		if (torchEquipped)
		{
			// Torch fix
			if (m_pluginConfig->General().bEnableThirdPersonTorch && !playerBlocking)
				return true;
			// Torch Block fix
			if (m_pluginConfig->General().bEnableThirdPersonTorchBlock && playerBlocking)
				return true;
		}

		if (shieldEquipped)
		{
			// Shield fix
			if (m_pluginConfig->General().bEnableThirdPersonShield && !playerBlocking)
				return true;
			// Shield Block fix
			if (m_pluginConfig->General().bEnableThirdPersonShieldBlock && playerBlocking)
				return true;
		}

		// Fists fix
		if (GetEquippedItemTypeID(player) == RE::EQUIPPED_ITEMTYPE_ID::kFist && GetEquippedItemTypeID(player, true) == RE::EQUIPPED_ITEMTYPE_ID::kFist)
			return false;

		// One Handed weapon fix
		if (Helper::IsRighthandWeaponEquipped(player) && !torchEquipped && !shieldEquipped && !playerBlocking && !m_pluginConfig->Fixes().bFirstPersonOverhaul)
			return true;

		return false;
	}

	bool ImprovedCameraSE::UseThirdPersonRightArm()
	{
		using namespace Address::Function;

		auto player = RE::PlayerCharacter::GetSingleton();
		auto playerState = player->AsActorState();
		bool weaponDrawn = playerState->IsWeaponDrawn();

		// CFPAO/Potion Drinking fix
		if (((playerState->IsSprinting() || player->IsInMidair()) && !weaponDrawn && !Helper::IsBeastMode() &&
			m_pluginConfig->Fixes().bFirstPersonOverhaul && !m_pluginConfig->Fixes().bOverrideVanillaArmsOnMovement) ||
			Events::Observer::Get()->IsCurrentAnimation("DrinkPotion"))
		{
			return false;
		}
		// Torch/Animated Interactions fix
		if ((GetEquippedItemTypeID(player) == RE::EQUIPPED_ITEMTYPE_ID::kTorch && !weaponDrawn) || Events::Observer::Get()->IsCurrentAnimation("TakeItem"))
			return true;

		// Bow fix
		if (GetEquippedItemTypeID(player) == RE::EQUIPPED_ITEMTYPE_ID::kBow && !Helper::IsAiming(player) && !Helper::archeryGameplayOverhaul)
			return true;

		return false;
	}

	void ImprovedCameraSE::UpdateSkeleton(bool show)
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto thirdpersonNode = player->Get3D(0)->AsNode();

		static std::vector<std::unique_ptr<NodeOverride>> overrides;

		// Don't clear if Tween is shown
		if (!m_TweenShown)
			overrides.clear();  // Reset all previous overrides

		m_TweenShown = false;

		// Make sure ICamera is ready.
		if (!m_ICamera)
			return;

		bool isWerewolf = m_ICamera->GetID() == RE::CameraStates::kThirdPerson && m_ICamera->GetStateID() == CameraThirdPerson::State::kWerewolfIdle;
		bool isVampireLord = m_ICamera->GetID() == RE::CameraStates::kThirdPerson && m_ICamera->GetStateID() == CameraThirdPerson::State::kVampireLordIdle;
		bool isScripted = m_ICamera->GetID() == RE::CameraStates::kThirdPerson && m_ICamera->GetStateID() == CameraThirdPerson::State::kScriptedIdle;

		// Hiding of the head in first/third person.
		if (!show)
		{
			// Tween comes in here for 1 frame, need to account for this so it doesn't screw up what has been done to the model(s).
			if (m_IsFakeCamera || (m_CurrentCameraID == RE::CameraStates::kTween && m_PreviousCameraID == RE::CameraStates::kFirstPerson))
				m_TweenShown = true;

			auto headNode = Helper::GetHeadNode(thirdpersonNode);

			if (headNode && !Helper::CannotMoveAndLook())
			{
				bool hideHead = !m_pluginConfig->General().bEnableHead && (m_CurrentCameraID == RE::CameraStates::kFirstPerson || m_CurrentCameraID == RE::CameraStates::kTween && m_PreviousCameraID == RE::CameraStates::kFirstPerson) && m_ICamera->GetStateID() != CameraFirstPerson::State::kWeaponDrawnIdle ||
					!m_pluginConfig->General().bEnableHeadCombat &&	(m_CurrentCameraID == RE::CameraStates::kFirstPerson || m_CurrentCameraID == RE::CameraStates::kTween && m_PreviousCameraID == RE::CameraStates::kFirstPerson) && m_ICamera->GetStateID() == CameraFirstPerson::State::kWeaponDrawnIdle ||
					!m_pluginConfig->General().bEnableHeadHorse && m_IsFakeCamera && (m_CurrentCameraID == RE::CameraStates::kMount || m_CurrentCameraID == RE::CameraStates::kTween && m_PreviousCameraID == RE::CameraStates::kMount) ||
					!m_pluginConfig->General().bEnableHeadDragon && m_IsFakeCamera && (m_CurrentCameraID == RE::CameraStates::kDragon || m_CurrentCameraID == RE::CameraStates::kTween && m_PreviousCameraID == RE::CameraStates::kDragon) ||
					!m_pluginConfig->General().bEnableHeadScripted && m_IsFakeCamera && isScripted ||
					!m_pluginConfig->General().bEnableHeadVampireLord && m_IsFakeCamera && isVampireLord ||
					!m_pluginConfig->General().bEnableHeadWerewolf && m_IsFakeCamera && isWerewolf;

				if (hideHead && isWerewolf)
				{
					headNode = Helper::FindNode(thirdpersonNode, "NPC Neck [Neck]");

					auto thirdpersonState = (RE::ThirdPersonState*)RE::PlayerCamera::GetSingleton()->currentState.get();

					if (thirdpersonState->targetZoomOffset == *fMinCurrentZoom)
						overrides.push_back(std::make_unique<NodeOverride>(headNode, 0.001f));
				}
				else if (hideHead && headNode->local.scale > 0.002f)
				{
					overrides.push_back(std::make_unique<NodeOverride>(headNode, 0.001f));
				}
			}
		}
		// Hiding of the arms/weaponary for first person.
		if (!show)
		{
			if (m_CurrentCameraID != RE::CameraStates::kFirstPerson && !(m_CurrentCameraID == RE::CameraStates::kTween && m_PreviousCameraID == RE::CameraStates::kFirstPerson))
				return;

			if (isWerewolf || isVampireLord || isScripted)
				return;

			// Check left arm
			auto leftArmNode = Helper::FindNode(thirdpersonNode, "NPC L UpperArm [LUar]");
			// Check right arm
			auto rightArmNode = Helper::FindNode(thirdpersonNode, "NPC R UpperArm [RUar]");
			// Check weapon back
			auto weaponBackNode = Helper::FindNode(thirdpersonNode, "WeaponBack");
			// Check weapon bow
			auto weaponBowNode = Helper::FindNode(thirdpersonNode, "WeaponBow");
			// Check quiver
			auto quiverNode = Helper::FindNode(thirdpersonNode, "QUIVER");

			if (!UseThirdPersonArms())
			{
				if (leftArmNode && !UseThirdPersonLeftArm() && leftArmNode->local.scale > 0.002f)
				{
					overrides.push_back(std::make_unique<NodeOverride>(leftArmNode, 0.001f));
				}
				if (rightArmNode && !UseThirdPersonRightArm() && rightArmNode->local.scale > 0.002f)
				{
					overrides.push_back(std::make_unique<NodeOverride>(rightArmNode, 0.001f));
				}
			}
			if (weaponBackNode && m_pluginConfig->Hide().b2HWeapon && weaponBackNode->local.scale > 0.002f)
			{
				overrides.push_back(std::make_unique<NodeOverride>(weaponBackNode, 0.001f));
			}
			if (weaponBowNode && m_pluginConfig->Hide().bBow && weaponBowNode->local.scale > 0.002f)
			{
				overrides.push_back(std::make_unique<NodeOverride>(weaponBowNode, 0.001f));
			}
			if (quiverNode && m_pluginConfig->Hide().bQuiver && quiverNode->local.scale > 0.002f)
			{
				overrides.push_back(std::make_unique<NodeOverride>(quiverNode, 0.001f));
			}
		}
	}

	void ImprovedCameraSE::FixWeaponPosition(RE::Actor* actor, RE::NiNode* firstpersonNode, RE::NiNode* thirdpersonNode)
	{
		if (UseThirdPersonArms() && actor->AsActorState()->IsWeaponDrawn())
		{
			auto fpWeaponNode = Helper::FindNode(firstpersonNode, "WEAPON");
			auto tpWeaponNode = Helper::FindNode(thirdpersonNode, "WEAPON");

			fpWeaponNode->world.scale = tpWeaponNode->world.scale;
			fpWeaponNode->world.rotate = tpWeaponNode->world.rotate;
			fpWeaponNode->world.translate = tpWeaponNode->world.translate;
		}
	}

	void ImprovedCameraSE::DisplayShadows(bool show)
	{
		auto thirdperson3D = RE::PlayerCharacter::GetSingleton()->Get3D(0);
		if (!thirdperson3D)
			return;

		RE::BSVisit::TraverseScenegraphGeometries(thirdperson3D, [&](RE::BSGeometry* a_geometry) -> RE::BSVisit::BSVisitControl {
			const auto& effect = a_geometry->properties[RE::BSGeometry::States::kEffect];
			const auto lightingShader = netimmerse_cast<RE::BSLightingShaderProperty*>(effect.get());

			if (lightingShader)
				lightingShader->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kCastShadows, show);

			return RE::BSVisit::BSVisitControl::kContinue;
		});
	}

	float ImprovedCameraSE::UpdateNearDistance(float fNear)
	{
		auto camera = RE::PlayerCamera::GetSingleton();
		if (!camera || !m_ICamera)
			return fNear;

		auto player = RE::PlayerCharacter::GetSingleton();
		auto firstpersonState = (RE::FirstPersonState*)camera->currentState.get();
		auto cameraNode = camera->cameraRoot.get()->AsNode();
		auto cameraNI = (RE::NiCamera*)((cameraNode->children.size() == 0) ? nullptr : cameraNode->children[0].get());
		if (!cameraNI)
			return fNear;

		// Fix flickering map
		if (RE::UI::GetSingleton()->IsMenuOpen("MapMenu"))
		{
			cameraNI->viewFrustum.fNear = 15.0f;
			return fNear; // 128.0f
		}

		float nearDistance = cameraNI->viewFrustum.fNear;  // 15.0f
		float maxPitch = m_pluginConfig->NearDistance().fPitchThreshold;

		if (m_pluginConfig->NearDistance().bEnableOverride && *m_ICamera->GetData().EventActive)
		{
			bool dialogueMenu = RE::UI::GetSingleton()->IsMenuOpen("Dialogue Menu");

			if (m_ICamera->GetID() == RE::CameraStates::kFirstPerson)
			{
				if (Helper::IsSittingOrSleeping(player))
					nearDistance = m_pluginConfig->NearDistance().fSitting;
				else if (m_ICamera->GetEventID() == CameraEvent::kFirstPersonCombat)
					nearDistance = *m_ICamera->GetData().NearDistance;
				else if (m_ICamera->GetEventID() == CameraEvent::kFirstPerson && firstpersonState->currentPitchOffset <= maxPitch)
					nearDistance = *m_ICamera->GetData().NearDistance;
				else
					nearDistance = m_pluginConfig->NearDistance().fFirstPersonDefault;
				// Fix for dismounting on the Intro cartride so the npc infront is fully seen.
				if (m_ICamera->GetStateID() == CameraFirstPerson::State::kCartRideDismounting)
					nearDistance = 2.0f;
			}
			else if (!m_IsFakeCamera || dialogueMenu)
				nearDistance = m_pluginConfig->NearDistance().fThirdPerson;

			else
				nearDistance = *m_ICamera->GetData().NearDistance;
		}
		else
			nearDistance = 15.0f;

		// fNear1stPersonDistance:Display check, usually comes in at 5.0f
		if (fNear < nearDistance)
		{
			cameraNI->viewFrustum.fNear = 15.0f;
			return fNear;
		}

		cameraNI->viewFrustum.fNear = nearDistance;

		return nearDistance;
	}

	void ImprovedCameraSE::UpdateFOV(RE::PlayerCamera* camera)
	{
		float fov = *fDefaultWorldFOV;  // 80.0f

		if (m_CurrentCameraID == RE::CameraStates::kFirstPerson)
		{
			float fovHands = m_pluginConfig->FOV().fFirstPersonHands;  // 80.0f

			if (camera->firstPersonFOV != fovHands)
				camera->firstPersonFOV = fovHands;
		}
		// Don't update during Dialogue/KeyHole menus
		auto ui = RE::UI::GetSingleton();
		bool dialogueMenu = ui->IsMenuOpen("Dialogue Menu");
		bool keyHole = ui->IsMenuOpen("Keyhole");

		if (!dialogueMenu && !keyHole)
		{
			if (m_pluginConfig->FOV().bEnableOverride && *m_ICamera->GetData().EventActive)
			{
				fov = *m_ICamera->GetData().FOV;

				if (camera->worldFOV != fov)
					camera->worldFOV = fov;
			}
		}
	}

	void ImprovedCameraSE::TranslateCamera()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto playerState = player->AsActorState();
		auto firstpersonNode = player->Get3D(1)->AsNode();
		auto thirdpersonNode = player->Get3D(0)->AsNode();
		auto headNode = Helper::GetHeadNode(thirdpersonNode);
		auto eyeBoneNode = Helper::FindNode(firstpersonNode, "NPCEyeBone");

		if (!headNode)
			return;

		auto cameraNode = RE::PlayerCamera::GetSingleton()->cameraRoot.get()->AsNode();
		auto cameraNI = (RE::NiCamera*)((cameraNode->children.size() == 0) ? nullptr : cameraNode->children[0].get());
		if (!cameraNI)
			return;

		RE::NiPoint3 point1{}, point2{}, lerp{};
		ScalePoint(&point1, thirdpersonNode->world.scale);
		Utils::MatrixVectorMultiply(&point2, &thirdpersonNode->world.rotate, &point1);
		float headRot = 0.0f;
		float lerpStrength = 0.0f;
		auto upDisplacement = (playerState->actorState1.sneaking ? 0.0f : eyeBoneNode->local.translate.z) + Helper::GetHighHeelsOffset(player);

		if (playerState->IsWeaponDrawn())
			upDisplacement = 0.0f;

		if (GetHeadRotation(&headRot) || m_IsFakeCamera ||
			m_ICamera->GetID() == RE::CameraStates::kMount && m_ICamera->GetStateID() >= CameraHorse::State::kMounted && m_ICamera->GetStateID() <= CameraHorse::State::kWeaponDrawnIdle)
		{
			if (HeadRotation())
			{
				if (m_ICamera->GetID() == RE::CameraStates::kThirdPerson && m_ICamera->GetStateID() == CameraThirdPerson::State::kWerewolfIdle)
				{
					if (!Helper::IsIdlePlaying(player))
					{
						headNode = Helper::FindNode(thirdpersonNode, "Camera3rd [Cam3]");

						if (playerState->IsSprinting())
							point1.z += -48.0f * thirdpersonNode->world.scale;
						else if (playerState->IsRunning())
							point1.y += 12.0f * thirdpersonNode->world.scale;
					}
					else
					{
						ScalePoint(&point1, headNode->world.scale);
						Utils::MatrixVectorMultiply(&point2, &headNode->world.rotate, &point1);
					}
				}

				if (m_IsFakeCamera)
					point1.y += 12.0f * thirdpersonNode->world.scale;

				Utils::MatrixVectorMultiply(&point2, &headNode->world.rotate, &point1);
			}
			if (!m_IsFakeCamera)
				lerpStrength = 0.17f;

			else
				lerpStrength = 1.0f;

			lerp = cameraNI->world.translate + ((headNode->world.translate + point2) - cameraNI->world.translate) * lerpStrength;
			lerp.z += upDisplacement;
			cameraNI->world.translate = cameraNode->world.translate = cameraNode->local.translate = lerp;
		}
		else
		{
			cameraNode->local.translate.z += upDisplacement;
			cameraNI->world.translate = cameraNode->world.translate = cameraNode->local.translate;
		}
		// For floating quest markers
		Helper::UpdateNode((RE::NiNode*)cameraNI, RE::NiUpdateData::Flag::kDirty);
	}

	void ImprovedCameraSE::TranslateFirstPersonModel()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto firstpersonNode = player->Get3D(1)->AsNode();
		auto thirdpersonNode = player->Get3D(0)->AsNode();
		auto headNode = Helper::GetHeadNode(thirdpersonNode);
		if (!headNode)
			return;

		auto cameraNode = RE::PlayerCamera::GetSingleton()->cameraRoot->AsNode();
		RE::NiPoint3 vFirstPerson{}, vThirdPerson{}, vTranslateRoot{}, point1{}, point2{};

		vFirstPerson = cameraNode->world.translate - firstpersonNode->world.translate;

		ScalePoint(&point1, thirdpersonNode->world.scale);
		Utils::MatrixVectorMultiply(&point2, &thirdpersonNode->world.rotate, &point1);

		vThirdPerson = (headNode->world.translate + point2) - thirdpersonNode->world.translate;
		vTranslateRoot = thirdpersonNode->local.translate - firstpersonNode->local.translate;
		firstpersonNode->local.translate += vThirdPerson - vFirstPerson + vTranslateRoot;
		// Height offset
		firstpersonNode->local.translate.z += m_pluginConfig->General().fBodyHeightOffset;
	}

	void ImprovedCameraSE::TranslateThirdPersonModel()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto firstpersonNode = player->Get3D(1)->AsNode();
		auto thirdpersonNode = player->Get3D(0)->AsNode();
		auto headNode = Helper::GetHeadNode(thirdpersonNode);
		if (!headNode)
			return;

		RE::NiPoint3 point1{}, point2{};
		float headRot = 0.0f;

		if (m_CurrentCameraID == RE::CameraStates::kFirstPerson ||
			m_CurrentCameraID == RE::CameraStates::kTween && m_PreviousCameraID == RE::CameraStates::kFirstPerson)
		{
			if (!GetHeadRotation(&headRot) && !m_IsFakeCamera)
			{
				// Update LootAt position
				UpdateLootAtPosition();
				AdjustModelPosition(point1, false);
				Utils::MatrixVectorMultiply(&point2, &thirdpersonNode->world.rotate, &point1);
				thirdpersonNode->local.translate += point2;
			}
			else
			{
				if (!m_IsFakeCamera)
				{
					// Update LootAt position
					UpdateLootAtPosition();
					AdjustModelPosition(point1, true);
				}
				Utils::MatrixVectorMultiply(&point2, &thirdpersonNode->world.rotate, &point1);
				thirdpersonNode->local.translate += point2;
			}
			thirdpersonNode->local.translate.z += m_pluginConfig->General().fBodyHeightOffset;

			FixWeaponPosition(player, firstpersonNode, thirdpersonNode);
		}
	}

	void ImprovedCameraSE::AdjustModelPosition(RE::NiPoint3& position, bool headbob)
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto playerState = player->AsActorState();
		auto thirdpersonNode = player->Get3D(0)->AsNode();
		auto headNode = Helper::GetHeadNode(thirdpersonNode);
		auto eyeNode = Helper::FindNode(thirdpersonNode, "NPCEyeBone");
		auto cameraNode = RE::PlayerCamera::GetSingleton()->cameraRoot.get()->AsNode();

		glm::vec3 cameraPosition = { cameraNode->world.translate.x, cameraNode->world.translate.y, cameraNode->world.translate.z };
		glm::vec3 eyePosition = { eyeNode->world.translate.x, eyeNode->world.translate.y, eyeNode->world.translate.z };
		glm::vec3 headPosition = { headNode->world.translate.x, headNode->world.translate.y, headNode->world.translate.z };

		auto eyeDisplacement = glm::distance(headPosition, eyePosition);
		auto headDisplacement = glm::distance(headPosition, cameraPosition);
		auto forwardDisplacement = abs(headDisplacement + eyeDisplacement);

		auto rightDisplacement = headNode->world.translate.x - cameraNode->world.translate.x;
		if (playerState->actorState1.sneaking)
			rightDisplacement = 0.0f;
		else if (playerState->actorState1.movingRight && !playerState->actorState1.movingForward && !playerState->actorState1.movingBack)
			rightDisplacement = abs(rightDisplacement);
		else if (playerState->actorState1.movingLeft && !playerState->actorState1.movingForward)
			rightDisplacement = -abs(rightDisplacement);
		else
			rightDisplacement = 0.0f;

		if (!headbob)
		{
			if (playerState->IsWeaponDrawn())
			{
				position.x = (-m_pluginConfig->Camera().fFirstPersonCombatPosX * thirdpersonNode->world.scale) - rightDisplacement;
				position.y = (-m_pluginConfig->Camera().fFirstPersonCombatPosY * thirdpersonNode->world.scale) - forwardDisplacement;
				position.z = -m_pluginConfig->Camera().fFirstPersonCombatPosZ * thirdpersonNode->world.scale;
			}
			else
			{
				position.x = (-m_pluginConfig->Camera().fFirstPersonPosX * thirdpersonNode->world.scale) - rightDisplacement;
				position.y = (-m_pluginConfig->Camera().fFirstPersonPosY * thirdpersonNode->world.scale) - forwardDisplacement;
				position.z = -m_pluginConfig->Camera().fFirstPersonPosZ * thirdpersonNode->world.scale;
			}
		}
		else
		{
			position.x = (0.0f * thirdpersonNode->world.scale) - rightDisplacement;

			if ((playerState->IsWeaponDrawn() && m_pluginConfig->Camera().fFirstPersonCombatPosY < 0.1f) || (!playerState->IsWeaponDrawn() && m_pluginConfig->Camera().fFirstPersonPosY < 0.1f))
				position.y = 0.0f * thirdpersonNode->world.scale;
			else
				position.y = (-8.0f * thirdpersonNode->world.scale) - forwardDisplacement;

			position.z = 0.0f * thirdpersonNode->world.scale;
		}
	}

	void ImprovedCameraSE::UpdateLootAtPosition()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto thirdpersonNode = player->Get3D(0)->AsNode();
		auto headNode = Helper::GetHeadNode(thirdpersonNode);
		if (!headNode)
			return;

		RE::NiPoint3 point1{}, point2{}, lookAtPosition{};
		point1.y = 500.0f;
		Utils::MatrixVectorMultiply(&point2, &thirdpersonNode->world.rotate, &point1);

		lookAtPosition = headNode->world.translate + point2;
		player->GetActorRuntimeData().currentProcess->SetHeadtrackTarget(player, lookAtPosition);
	}

	bool ImprovedCameraSE::GetHeadRotation(float* rotation)
	{
		auto playerState = RE::PlayerCharacter::GetSingleton()->AsActorState();

		if (playerState->IsWeaponDrawn())
		{
			if (m_pluginConfig->Headbob().bCombat)
			{
				// Disabled to figure out why weapon node is acting wonky.
				*rotation = m_pluginConfig->Headbob().fRotationCombat;
				return false;
			}
		}
		else
		{
			if (m_pluginConfig->Headbob().bSneakRoll && playerState->IsSneaking() && playerState->IsSprinting())
			{
				*rotation = m_pluginConfig->Headbob().fRotationSneakRoll;
				return true;
			}
			else if (m_pluginConfig->Headbob().bSneak && playerState->IsSneaking())
			{
				*rotation = m_pluginConfig->Headbob().fRotationSneak;
				return true;
			}
			else if (m_pluginConfig->Headbob().bSprint && playerState->IsSprinting())
			{
				*rotation = m_pluginConfig->Headbob().fRotationSprint;
				return true;
			}
			else if (m_pluginConfig->Headbob().bWalk && playerState->IsWalking())
			{
				*rotation = m_pluginConfig->Headbob().fRotationWalk;
				return true;
			}
			else if (m_pluginConfig->Headbob().bRun && playerState->IsRunning())
			{
				*rotation = m_pluginConfig->Headbob().fRotationRun;
				return true;
			}
			else if (m_pluginConfig->Headbob().bIdle && !playerState->IsWalking() && !playerState->IsRunning() && !playerState->IsSprinting() &&
				!playerState->IsSneaking() && !playerState->IsSwimming())
			{
				*rotation = m_pluginConfig->Headbob().fRotationIdle;
				return true;
			}
		}
		return false;
	}

	bool ImprovedCameraSE::HeadRotation()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto camera = RE::PlayerCamera::GetSingleton();
		auto firstpersonState = (RE::FirstPersonState*)camera->cameraStates[RE::CameraState::kFirstPerson].get();
		auto thirdpersonNode = player->Get3D(0)->AsNode();
		auto headNode = Helper::GetHeadNode(thirdpersonNode);
		auto cameraNode = camera->cameraRoot.get()->AsNode();
		auto isWerewolf = m_ICamera->GetID() == RE::CameraState::kThirdPerson && (m_ICamera->GetStateID() == CameraThirdPerson::State::kWerewolfEnter || m_ICamera->GetStateID() == CameraThirdPerson::State::kWerewolfIdle);
		auto cameraNI = (RE::NiCamera*)((cameraNode->children.size() == 0) ? nullptr : cameraNode->children[0].get());
		if (!cameraNI)
			return false;

		RE::NiPoint3 point{}, angle{};
		RE::NiMatrix3 matrix1{}, matrix2{};
		RE::NiQuaternion quat1{}, quat2{}, rQuat{};
		float headRot = 0.0f;

		firstpersonState->firstPersonCameraObj->world.rotate.ToEulerAnglesXYZ(angle);
		point.x = -angle.x - player->data.angle.x;  // Pitch
		point.y = M_PI * 0.5f;                      // Yaw
		point.z = M_PI * 0.5f;                      // Roll
		Utils::EulerToMatrix(&matrix1, point.x, point.y, point.z);

		if (m_ICamera->GetID() == RE::CameraState::kFirstPerson && m_ICamera->GetStateID() == CameraFirstPerson::State::kSittingIdle)
			headNode->world.rotate = thirdpersonNode->world.rotate;

		if ((!m_IsFakeCamera && GetHeadRotation(&headRot)) ||
			(m_ICamera->GetID() == RE::CameraStates::kMount && m_ICamera->GetStateID() >= CameraHorse::State::kMounted && m_ICamera->GetStateID() <= CameraHorse::State::kRiding) ||
			(isWerewolf && !Helper::IsIdlePlaying(player)))

		{
			if (Helper::IsSittingOrSleeping(player))
				headRot = 0.0f;

			matrix2 = headNode->world.rotate * matrix1;

			Utils::RotMatrixToQuaternion(&cameraNI->world.rotate, &quat1);
			Utils::RotMatrixToQuaternion(&matrix2, &quat2);
			Utils::SlerpQuat(&rQuat, &quat1, &quat2, headRot);

			Utils::QuaternionToMatrix(&rQuat, &cameraNI->world.rotate);
			Utils::MatrixInverse(&cameraNI->local.rotate, &matrix1);

			matrix2 = cameraNI->world.rotate * matrix1;
			cameraNode->world.rotate = cameraNode->local.rotate = matrix2;

			if (isWerewolf)
				return true;
		}
		else
		{
			auto thirdpersonState = (RE::ThirdPersonState*)camera->currentState.get();

			if (!thirdpersonState->IsInputEventHandlingEnabled() ||  // Crafting
				camera->currentState->id == RE::CameraState::kVATS ||
				camera->currentState->id == RE::CameraState::kPCTransition ||
				camera->currentState->id == RE::CameraState::kAnimated ||
				camera->currentState->id == RE::CameraState::kBleedout ||
				m_ICamera->GetID() == RE::CameraState::kMount && m_ICamera->GetEventID() == CameraEvent::kHorseTransition ||
				isWerewolf && Helper::IsIdlePlaying(player))
			{
				// Clamp the direction for crafting and Werewolf feeding.
				if (!thirdpersonState->IsInputEventHandlingEnabled() || isWerewolf)
				{
					if (point.x < -0.45f)
					{
						point.x = -0.45f;
						player->data.angle.x = abs(point.x);
					}
					if (point.x > 0.0f)
					{
						point.x = 0.0f;
						player->data.angle.x = point.x;
					}
				}
				Utils::EulerToMatrix(&matrix1, point.x, point.y, point.z);
				cameraNI->world.rotate = headNode->world.rotate * matrix1;
			}
			// Scripted and Animation
			if (m_ICamera->GetID() == RE::CameraState::kThirdPerson &&
				(m_ICamera->GetStateID() == CameraThirdPerson::State::kScriptedIdle || m_ICamera->GetStateID() == CameraThirdPerson::State::kAnimationIdle))
			{
				point.x = M_PI * 0.5f - player->data.angle.x;              // Pitch
				point.y = M_PI * 0.5f - thirdpersonState->freeRotation.x;  // Yaw
				point.z = thirdpersonState->freeRotation.y;                // Roll

				Utils::EulerToMatrix(&matrix1, point.x, point.y, point.z);
				cameraNI->world.rotate = headNode->world.rotate * matrix1;
			}
			Utils::MatrixInverse(&cameraNI->local.rotate, &matrix2);
			matrix1 = cameraNI->world.rotate * matrix2;
			cameraNode->world.rotate = cameraNode->local.rotate = matrix1;

			return true;
		}
		return false;
	}

	void ImprovedCameraSE::ScalePoint(RE::NiPoint3* point, float scale)
	{
		if (!m_ICamera)
			return;

		if (m_ICamera->GetID() == RE::CameraStates::kThirdPerson && m_ICamera->GetStateID() == CameraThirdPerson::State::kScriptedIdle)
		{
			point->x = m_pluginConfig->Camera().fScriptedPosX * scale;
			point->y = m_pluginConfig->Camera().fScriptedPosY * scale;
			point->z = m_pluginConfig->Camera().fScriptedPosZ * scale;
		}
		else if (m_ICamera->GetID() == RE::CameraStates::kThirdPerson && m_ICamera->GetStateID() == CameraThirdPerson::State::kNecroLichIdle)
		{
			point->x = m_pluginConfig->Camera().fNecroLichPosX * scale;
			point->y = m_pluginConfig->Camera().fNecroLichPosY * scale;
			point->z = m_pluginConfig->Camera().fNecroLichPosZ * scale;
		}
		else if (m_ICamera->GetID() == RE::CameraStates::kThirdPerson && m_ICamera->GetStateID() == CameraThirdPerson::State::kWerewolfIdle)
		{
			point->x = m_pluginConfig->Camera().fWerewolfPosX * scale;
			point->y = (m_pluginConfig->Camera().fWerewolfPosY + 48.0f) * scale;
			point->z = (m_pluginConfig->Camera().fWerewolfPosZ - 16.0f) * scale;
		}
		else if (m_ICamera->GetID() == RE::CameraStates::kThirdPerson && m_ICamera->GetStateID() == CameraThirdPerson::State::kVampireLordIdle)
		{
			point->x = m_pluginConfig->Camera().fVampireLordPosX * scale;
			point->y = m_pluginConfig->Camera().fVampireLordPosY * scale;
			point->z = m_pluginConfig->Camera().fVampireLordPosZ * scale;
		}
		else if (m_ICamera->GetID() == RE::CameraStates::kDragon && m_ICamera->GetStateID() == CameraDragon::State::kRiding)
		{
			point->x = m_pluginConfig->Camera().fDragonPosX * scale;
			point->y = m_pluginConfig->Camera().fDragonPosY * scale;
			point->z = m_pluginConfig->Camera().fDragonPosZ * scale;
		}
		else if (m_ICamera->GetID() == RE::CameraStates::kMount && m_ICamera->GetStateID() == CameraHorse::State::kWeaponDrawnIdle)
		{
			auto player = RE::PlayerCharacter::GetSingleton();
			if (!Helper::IsAiming(player))
				point->x = m_pluginConfig->Camera().fHorseCombatPosX * scale;
			else
				point->x = (m_pluginConfig->Camera().fHorseCombatPosX - 8.0f) * scale;

			point->y = m_pluginConfig->Camera().fHorseCombatPosY * scale;
			point->z = m_pluginConfig->Camera().fHorseCombatPosZ * scale;
		}
		else if (m_ICamera->GetID() == RE::CameraStates::kMount && m_ICamera->GetStateID() == CameraHorse::State::kRiding)
		{
			point->x = m_pluginConfig->Camera().fHorsePosX * scale;
			point->y = m_pluginConfig->Camera().fHorsePosY * scale;
			point->z = m_pluginConfig->Camera().fHorsePosZ * scale;
		}
		else if (m_ICamera->GetID() == RE::CameraStates::kFirstPerson && m_ICamera->GetStateID() == CameraFirstPerson::State::kWeaponDrawnIdle)
		{
			point->x = m_pluginConfig->Camera().fFirstPersonCombatPosX * scale;
			point->y = m_pluginConfig->Camera().fFirstPersonCombatPosY * scale;
			point->z = m_pluginConfig->Camera().fFirstPersonCombatPosZ * scale;
		}
		else
		{
			point->x = m_pluginConfig->Camera().fFirstPersonPosX * scale;
			point->y = m_pluginConfig->Camera().fFirstPersonPosY * scale;
			point->z = m_pluginConfig->Camera().fFirstPersonPosZ * scale;
		}
	}

	void ImprovedCameraSE::ReleaseAPIs()
	{
		if (m_SmoothCamAPI && m_SmoothCamSnapshot)
		{
			m_SmoothCamSnapshot = false;
			m_SmoothCamAPI->ReleaseCameraControl(m_ICHandle);
		}
		if (m_TDMAPI && m_TDMSnapshot)
		{
			m_TDMSnapshot = false;
			m_TDMAPI->ReleaseDisableHeadtracking(m_ICHandle);
			*m_directionalMovementSheathed = m_directionalMovementSheathedOriginal;
			*m_directionalMovementDrawn = m_directionalMovementDrawnOriginal;
		}
	}

	void ImprovedCameraSE::SetupCameraData()
	{
		m_Camera.Register(new CameraDeathCinematic);
		m_Camera.Register(new CameraFurniture);
		m_Camera.Register(new CameraTransition);
		m_Camera.Register(new CameraThirdPerson);
		m_Camera.Register(new CameraHorse);
		m_Camera.Register(new CameraRagdoll);
		m_Camera.Register(new CameraDragon);
		m_Camera.Register(new CameraFirstPerson);
	}

}
