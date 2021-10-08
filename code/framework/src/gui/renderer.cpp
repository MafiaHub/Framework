/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "renderer.h"

namespace Framework::GUI {
    RendererError Renderer::Init(RendererConfiguration config){
        if(_initialized){
            return RendererError::RENDERER_ALREADY_INITIALIZED;
        }

        _config = config;
        switch(config.api){
            case RendererAPI::CEF: {
                //TODO: implement
            } break;

            case RendererAPI::ULTRALIGHT: {
                //TODO: implement
            } break;

            default:
                return RendererError::RENDERER_UNKNOWN_API;
        }

        _initialized = true;
        return RendererError::RENDERER_NONE;
    }

    RendererError Renderer::Shutdown(){
        if(!_initialized){
            return RendererError::RENDERER_NOT_INITIALIZED;
        }

        switch(_config.api){
            case RendererAPI::CEF: {
                //TODO: implement
            } break;

            case RendererAPI::ULTRALIGHT: {
                //TODO: implement
            }

            default:
                return RendererError::RENDERER_UNKNOWN_API;
        }

        _initialized = false;
        return RendererError::RENDERER_NONE;
    }
}
