/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <NetworkIDObject.h>
#include <RakNetTypes.h>
#include <glm/ext.hpp>

namespace Framework::Networking::Entities {
    class Entity: public SLNet::NetworkIDObject {
      public:
        enum class EntityType : int { NONE, PLAYER, NPC, VEHICLE, OBJECT, PICKUP };

        Entity();

        Entity(SLNet::RakNetGUID, EntityType, int);

        ~Entity() override = default;

        virtual EntityType GetType() const {
            return _type;
        }

        virtual void SetType(EntityType type) {
            _type = type;
        }

        virtual SLNet::RakNetGUID GetOwner() const {
            return _owner;
        }

        virtual void SetOwner(SLNet::RakNetGUID owner) {
            _owner = owner;
        }

        virtual int GetVirtualWorld() const {
            return _virtualWorld;
        }

        virtual void SetVirtualWorld(int world) {
            _virtualWorld = world;
        }

        virtual int GetModelId() const {
            return _modelId;
        }

        virtual void SetModelId(int id) {
            _modelId = id;
        }

        virtual void SetPosition(glm::vec3 &pos) {
            _position = pos;
        }

        virtual glm::vec3 GetPosition() const {
            return _position;
        }

        virtual void SetRotation(glm::quat &rot) {
            _rotation = rot;
        }

        virtual glm::quat GetRotation() const {
            return _rotation;
        }

        virtual bool IsSpawned() const {
            return _isSpawned;
        }

        virtual bool IsPendingRemoval() const {
            return _isPendingRemoval;
        }

        virtual void MarkForRemoval() {
            _isPendingRemoval = true;
        }

        virtual void Update() {}

        virtual void Spawn() {
            _isSpawned = true;
        }

        virtual void Despawn() {
            _isSpawned = false;
        }

      private:
        SLNet::RakNetGUID _owner = SLNet::UNASSIGNED_RAKNET_GUID;
        EntityType _type;

        int _virtualWorld      = 0;
        int _modelId           = 0;
        bool _isSpawned        = false;
        bool _isPendingRemoval = false;

        glm::vec3 _position = {0.0f, 0.0f, 0.0f};
        glm::quat _rotation = {0.0f, 0.0f, 0.0f, 0.0f};
    };
} // namespace Framework::Networking::Entities
