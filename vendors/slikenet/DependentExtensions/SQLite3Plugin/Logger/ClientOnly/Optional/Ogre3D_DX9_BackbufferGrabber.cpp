/*
 * This file was taken from RakNet 4.082 without any modifications.
 * Please see licenses/RakNet license.txt for the underlying license and related copyright.
 */

#include "Ogre3D_DX9_BackbufferGrabber.h"

void Ogre3D_DX9_BackbufferGrabber::InitBackbufferGrabber(Ogre::RenderWindow* renderWindow, int _width, int _height)
{
	LPDIRECT3DDEVICE9 DX9Device;
	renderWindow->getCustomAttribute("D3DDEVICE", &DX9Device);
	DX9_BackbufferGrabber::InitBackbufferGrabber(DX9Device, _width, _height);
}
