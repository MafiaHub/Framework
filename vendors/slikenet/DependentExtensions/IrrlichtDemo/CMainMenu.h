// This is a Demo of the Irrlicht Engine (c) 2005 by N.Gebhardt.
// This file is not documentated.

/*
 * This file was taken from RakNet 4.082.
 *
 * Modified work: Copyright (c) 2017, SLikeSoft UG (haftungsbeschränkt)
 *
 * This source code was modified by SLikeSoft. Modifications are licensed under the MIT-style
 * license found in the license.txt file in the root directory of this source tree.
 * Alternatively you are permitted to license the modifications under the zlib/libpng license.
 */

#ifndef __C_MAIN_MENU_H_INCLUDED__
#define __C_MAIN_MENU_H_INCLUDED__

#include <irrlicht.h>
#ifdef _WIN32__
#include "slikenet/WindowsIncludes.h" // Prevent 'fd_set' : 'struct' type redefinition
#endif
#include "IGUIEditBox.h"

using namespace irr;

class CMainMenu : public IEventReceiver
{
public:

	CMainMenu();

	bool run(bool& outFullscreen, bool& outMusic, bool& outShadows,
		bool& outAdditive, bool &outVSync, bool& outAA,
		video::E_DRIVER_TYPE& outDriver,
		core::stringw &playerName);

	virtual bool OnEvent(const SEvent& event);

private:

	void setTransparency();

	gui::IGUIButton* startButton;
	IrrlichtDevice *MenuDevice;
	s32 selected;
	bool start;
	bool fullscreen;
	bool music;
	bool shadows;
	bool additive;
	bool transparent;
	bool vsync;
	bool aa;

	scene::IAnimatedMesh* quakeLevel;
	scene::ISceneNode* lightMapNode;
	scene::ISceneNode* dynamicNode;

	video::SColor SkinColor [ gui::EGDC_COUNT ];
	void getOriginalSkinColor();

	// RakNet: Store the edit box pointer so we can get the text later
	irr::gui::IGUIEditBox* nameEditBox;
};

#endif

