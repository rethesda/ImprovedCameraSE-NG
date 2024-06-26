/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

// Precompiled Header
#include "PCH.h"

#include "menu/Camera.h"

namespace Menu {

	struct MENU_IDS {

		enum MENU_ID : std::int32_t
		{
			kFirstPersonPosX = 1,
			kFirstPersonPosY,
			kFirstPersonPosZ,
			kFirstPersonCombatPosX,
			kFirstPersonCombatPosY,
			kFirstPersonCombatPosZ,
			kHorsePosX,
			kHorsePosY,
			kHorsePosZ,
			kHorseCombatPosX,
			kHorseCombatPosY,
			kHorseCombatPosZ,
			kDragonPosX,
			kDragonPosY,
			kDragonPosZ,
			// Table 2
			kVampireLordPosX,
			kVampireLordPosY,
			kVampireLordPosZ,
			kWerewolfPosX,
			kWerewolfPosY,
			kWerewolfPosZ,
			kNecroLichPosX,
			kNecroLichPosY,
			kNecroLichPosZ,
			kScriptedPosX,
			kScriptedPosY,
			kScriptedPosZ,

			kTotal = 28
		};
	};
	using MENU_ID = MENU_IDS::MENU_ID;

	MenuCamera::MenuCamera()
	{
		// Table 1
		m_MenuNodes.emplace_back(1, "Player Position X", "Sets First Person Camera Position X Relative to the Head Node if Headbob is Enable, otherwise Moves the Body",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fFirstPersonPosX, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(1, "Player Position Y", "Sets First Person Camera Position Y Relative to the Head Node if Headbob is Enable, otherwise Moves the Body",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fFirstPersonPosY, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(1, "Player Position Z", "Sets First Person Camera Position Z Relative to the Head Node if Headbob is Enable, otherwise Moves the Body",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fFirstPersonPosZ, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(1, "Player Combat Position X", "Sets First Person Camera Combat Position X Relative to the Head Node if Headbob is Enable, otherwise Moves the Body",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fFirstPersonCombatPosX, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(1, "Player Combat Position Y", "Sets First Person Camera Combat Position Y Relative to the Head Node if Headbob is Enable, otherwise Moves the Body",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fFirstPersonCombatPosY, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(1, "Player Combat Position Z", "Sets First Person Camera Combat Position Z Relative to the Head Node if Headbob is Enable, otherwise Moves the Body",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fFirstPersonCombatPosZ, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(1, "Horse Position X", "Sets First Person Camera Horse Riding Position X Relative to the Head Node",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fHorsePosX, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(1, "Horse Position Y", "Sets First Person Camera Horse Riding Position Y Relative to the Head Node",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fHorsePosY, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(1, "Horse Position Z", "Sets First Person Camera Horse Riding Position Z Relative to the Head Node",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fHorsePosZ, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(1, "Horse Combat Position X", "Sets First Person Camera Horse Riding Combat Position X Relative to the Head Node",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fHorseCombatPosX, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(1, "Horse Combat Position Y", "Sets First Person Camera Horse Riding Combat Position Y Relative to the Head Node",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fHorseCombatPosY, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(1, "Horse Combat Position Z", "Sets First Person Camera Horse Riding Combat Position Z Relative to the Head Node",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fHorseCombatPosZ, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(1, "Dragon Position X", "Sets First Person Camera Dragon Riding Position X Relative to the Head Node",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fDragonPosX, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(1, "Dragon Position Y", "Sets First Person Camera Dragon Riding Position Y Relative to the Head Node",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fDragonPosY, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(1, "Dragon Position Z", "Sets First Person Camera Dragon Riding Position Z Relative to the Head Node",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fDragonPosZ, -8192.0f, 8192.0f, "%.1f");
		// Table 2
		m_MenuNodes.emplace_back(2, "Vampire Lord Position X", "Sets First Person Camera Vampire Lord Position X Relative to the Head Node",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fVampireLordPosX, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(2, "Vampire Lord Position Y", "Sets First Person Camera Vampire Lord Position Y Relative to the Head Node",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fVampireLordPosY, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(2, "Vampire Lord Position Z", "Sets First Person Camera Vampire Lord Position Z Relative to the Head Node",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fVampireLordPosZ, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(2, "Werewolf Position X", "Sets First Person Camera Werewolf Position X Relative to the Head Node",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fWerewolfPosX, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(2, "Werewolf Position Y", "Sets First Person Camera Werewolf Position Y Relative to the Head Node",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fWerewolfPosY, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(2, "Werewolf Position Z", "Sets First Person Camera Werewolf Position Z Relative to the Head Node",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fWerewolfPosZ, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(2, "Lich Position X", "Sets First Person Camera Lich Position X Relative to the Head Node",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fNecroLichPosX, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(2, "Lich Position Y", "Sets First Person Camera Lich Position Y Relative to the Head Node",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fNecroLichPosY, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(2, "Lich Position Z", "Sets First Person Camera Lich Position Z Relative to the Head Node",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fNecroLichPosZ, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(2, "Scripted Position X", "Sets the camera position X for scripted forced third person",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fScriptedPosX, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(2, "Scripted Position Y", "Sets the camera position Y for scripted forced third person",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fScriptedPosY, -8192.0f, 8192.0f, "%.1f");
		m_MenuNodes.emplace_back(2, "Scripted Position Z", "Sets the camera position Z for scripted forced third person",
			ControlType::kSliderFloat, (void*)&m_pluginConfig->m_Camera.fScriptedPosZ, -8192.0f, 8192.0f, "%.1f");
	}

	void MenuCamera::OnOpen()
	{
		ImGui::Checkbox("Camera", &m_Window);
	}

	void MenuCamera::OnUpdate()
	{
		if (!m_Window)
			return;

		ImGui::Begin("[CAMERA]", &m_Window, ImGuiWindowFlags_NoCollapse);

		ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "CTRL+CLICK on the sliders to input your desired value");
		ImGui::Separator();

		DisplayMenuNodes("CameraTable");
		ImGui::SameLine();
		DisplayMenuNodes("CameraTable", 2);

		if (ImGui::Button("Close"))
			m_Window = false;

		ImGui::End();
	}

}
