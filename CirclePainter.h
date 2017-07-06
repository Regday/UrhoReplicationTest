#pragma once

// #include <Urho3D/Input/Controls.h>
#include <Urho3D/Scene/LogicComponent.h>

using namespace Urho3D;

class CirclePainter : public LogicComponent
{
	URHO3D_OBJECT(CirclePainter, LogicComponent);
public:
	CirclePainter(Context* ctx);

	virtual void Start();
	virtual void Stop();

	static void CirclePainter::RegisterObject(Context* context);

	// CLient should take control over entity
	void TakeAuthority();
	void ResetAuthority();

	void OnMouseUp(StringHash type, VariantMap& args);

	void		SetColor(const Color& value);
	Color		GetColor() const;

private:
	Color				_color;
};