#include <Urho3D/Core/Context.h>
#include <Urho3D/Graphics/AnimationController.h>
#include <Urho3D/IO/MemoryBuffer.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Scene/SceneEvents.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Network/Network.h>
#include <Urho3D/Network/NetworkEvents.h>
#include <Urho3D/Urho2D/StaticSprite2D.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Graphics/Material.h>
#include "../Resource/ResourceCache.h"
#include "../Resource/ResourceEvents.h"

#include "CirclePainter.h"
#include "Common.h"

CirclePainter::CirclePainter(Context* ctx) : LogicComponent(ctx)
{
}

void CirclePainter::Start()
{
}

void CirclePainter::Stop()
{
}

void CirclePainter::RegisterObject(Context* context)
{
	context->RegisterFactory<CirclePainter>();
	URHO3D_ATTRIBUTE("Color", Color, _color, Color::WHITE, AM_DEFAULT);
}

void CirclePainter::TakeAuthority()
{
	SubscribeToEvent(E_MOUSEBUTTONUP, URHO3D_HANDLER(CirclePainter, OnMouseUp));
}

void CirclePainter::ResetAuthority()
{
	UnsubscribeFromEvent(E_MOUSEBUTTONUP);
}

void CirclePainter::OnMouseUp(StringHash type, VariantMap& args)
{
	Input* input = GetSubsystem<Input>();
	int b = args[MouseButtonDown::P_BUTTON].GetInt();

	if (b == MOUSEB_LEFT)
	{
		Network* network = GetSubsystem<Network>();
		Connection* serverConnection = network->GetServerConnection();
		if (serverConnection)
		{
			VariantMap packet;
			packet[P_ID] = GetNode()->GetID();
			Graphics* graphics = GetSubsystem<Graphics>();

			// Convert click position to world space. Camera at (0,0,0) always and looking forward
			Vector2 halfSize(graphics->GetWidth() / 2.0f, graphics->GetHeight() / 2.0f);
			IntVector2 v = input->GetMousePosition();
			Vector2 pos(v.x_, v.y_);
			pos = halfSize - pos;
			pos *= PIXEL_SIZE;
			pos.x_ = -pos.x_;

			packet[P_DC_POSITION] = pos;
			serverConnection->SendRemoteEvent(E_DRAWCOMMAND_REQUEST, true, packet);
		}
	}
}

void CirclePainter::SetColor(const Color& color)
{
	_color = color;
	MarkNetworkUpdate();
}

Color CirclePainter::GetColor() const
{
	return _color;
}