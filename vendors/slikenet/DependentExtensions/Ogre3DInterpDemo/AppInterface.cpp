/*
 *  Original work: Copyright (c) 2014, Oculus VR, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  RakNet License.txt file in the licenses directory of this source tree. An additional grant 
 *  of patent rights can be found in the RakNet Patents.txt file in the same directory.
 *
 *
 *  Modified work: Copyright (c) 2016-2017, SLikeSoft UG (haftungsbeschränkt)
 *
 *  This source code was modified by SLikeSoft. Modifications are licensed under the MIT-style
 *  license found in the license.txt file in the root directory of this source tree.
 */

#include "AppInterface.h"
#include "slikenet/assert.h"
#include "FSM.h"
#include "RunnableState.h"
#include "slikenet/linux_adapter.h"
#include "slikenet/osx_adapter.h"

#ifdef _CONSOLE
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#endif

AppInterface::AppInterface()
{
	hasFocus=false;
	primaryFSM=0;
	primaryStates=0;
	quit=false;
	primaryStatesLength=0;
	lastElapsedTimeMS=0;
	lastCurTimeMS=0;
}
AppInterface::~AppInterface()
{


}
void AppInterface::PreConfigure(void)
{
	primaryFSM = new FSM;
}
void AppInterface::PostConfigure(const char *defaultResourceConfigurationPath)
{
}
void AppInterface::Update(AppTime curTimeMS,AppTime elapsedTimeMS)
{
	lastCurTimeMS=curTimeMS;
	lastElapsedTimeMS=elapsedTimeMS;	
}
void AppInterface::OnAppShutdown(void)
{
	delete primaryFSM;
	if (primaryStates)
	{
		int i;
		for (i=0; i < primaryStatesLength; i++)
			delete primaryStates[i];
		delete [] primaryStates;
	}
}
void AppInterface::DebugOut(unsigned int lifetimeMS, const char *format, ...)
{
#ifdef _CONSOLE
	char text[8096];
	va_list ap;
	va_start(ap, format);
	vsnprintf_s(text, 8095, format, ap);
	va_end(ap);
	strcat(text, "\n");
	text[8095]=0;
	printf(text);
#else
	// Don't call this without an implementation.  Perhaps you meant to use MainApp() instead
	RakAssert(0);
#endif
}
bool AppInterface::HasFocus(void) const
{
	return hasFocus;
}
void AppInterface::SetFocus(bool hasFocus)
{
	if (this->hasFocus!=hasFocus)
	{
		if (primaryFSM->CurrentState())
			((RunnableState*)(primaryFSM->CurrentState()))->SetFocus(hasFocus);
		this->hasFocus=hasFocus;
	}
}
void AppInterface::PushState(RunnableState* state)
{
	if (hasFocus && primaryFSM->CurrentState())
		((RunnableState*)(primaryFSM->CurrentState()))->SetFocus(false);
	primaryFSM->AddState(state);
	if (hasFocus)
		((RunnableState*)(primaryFSM->CurrentState()))->SetFocus(true);
	else
		((RunnableState*)(primaryFSM->CurrentState()))->SetFocus(false);
}
void AppInterface::PushState(int stateType)
{
	PushState(primaryStates[stateType]);
}
void AppInterface::PopState(int popCount)
{
	RakAssert(popCount>=1);
	RakAssert(primaryFSM->GetStateHistorySize()>=1+popCount);
	if (hasFocus && primaryFSM->CurrentState())
		((RunnableState*)(primaryFSM->CurrentState()))->SetFocus(false);
	primaryFSM->SetPriorState(primaryFSM->GetStateHistorySize()-1-popCount);
	if (hasFocus)
		((RunnableState*)(primaryFSM->CurrentState()))->SetFocus(true);
}
RunnableState* AppInterface::GetCurrentState(void) const
{
	return (RunnableState*) primaryFSM->CurrentState();
}
int AppInterface::GetStateHistorySize(void) const
{
	return primaryFSM->GetStateHistorySize();
}
void AppInterface::AllocateStates(int numStates)
{
	RakAssert(primaryStates==0);
	primaryStates = new RunnableState*[numStates];

	primaryStatesLength=numStates;
}
void AppInterface::SetState(int stateType, RunnableState* state)
{
	primaryStates[stateType] = state;
	state->SetParentApp(this);
	state->OnStateReady();
}
RunnableState* AppInterface::GetState(int stateType) const
{
	return primaryStates[stateType];
}
bool AppInterface::ShouldQuit(void) const
{
	return quit;
}
void AppInterface::Quit(void)
{
	quit=true;
}
AppTime AppInterface::GetLastCurrentTime(void) const
{
	return lastCurTimeMS;
}
AppTime AppInterface::GetLastElapsedTime(void) const
{
	return lastElapsedTimeMS;
}
