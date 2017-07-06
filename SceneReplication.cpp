//
// Copyright (c) 2008-2016 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/Light.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/Input/Controls.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Network/Connection.h>
#include <Urho3D/Network/Network.h>
#include <Urho3D/Network/NetworkEvents.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Button.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/LineEdit.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>
#include <Urho3D/UI/UIEvents.h>
#include <Urho3D/Math/MathDefs.h>
#include <Urho3D/Urho2D/StaticSprite2D.h>
#include <Urho3D/Graphics/Technique.h>
#include <Urho3D/Graphics/Texture2D.h>

#include "SceneReplication.h"

#include "CirclePainter.h"

#include <Urho3D/DebugNew.h>

static const int DRAWING_TABLE_SIZE = 512;

// UDP port we will use
static const unsigned short SERVER_PORT = 2345;
// Identifier for our custom remote event we use to tell the client which object they control
static const StringHash E_CLIENTOBJECTID("ClientObjectID");
// Identifier for the node ID parameter in the event data
static const StringHash P_ID("ID");

static const StringHash E_DRAWCOMMAND_REQUEST("DrawCommandRequest");
static const StringHash E_DRAWCOMMAND_CONFIRM("DrawCommandConfirm");
static const StringHash P_DC_POSITION("DrawCommandPosition");
static const StringHash P_DC_COLOR("DrawCommandColor");

// Control bits we define
static const unsigned CTRL_FORWARD = 1;
static const unsigned CTRL_BACK = 2;
static const unsigned CTRL_LEFT = 4;
static const unsigned CTRL_RIGHT = 8;

URHO3D_DEFINE_APPLICATION_MAIN(SceneReplication)

SceneReplication::SceneReplication(Context* context) :
	Sample(context), clientObjectAuth_(false)
{
	CirclePainter::RegisterObject(context);
}

void SceneReplication::Start()
{
	context_->RegisterSubsystem(new Console(context_));

    // Execute base class startup
    Sample::Start();

    // Create the scene content
    CreateScene();

    // Create the UI content
    CreateUI();

    // Setup the viewport for displaying the scene
    SetupViewport();

    // Hook up to necessary events
    SubscribeToEvents();

    // Set the mouse mode to use in the sample
    Sample::InitMouseMode(MM_FREE);
}

void SceneReplication::Stop()
{
	GetSubsystem<Log>()->Close();
}

void SceneReplication::CreateScene()
{
    scene_ = new Scene(context_);

    // Create scene content on the server only
    ResourceCache* cache = GetSubsystem<ResourceCache>();

    // Create octree and physics world with default settings. Create them as local so that they are not needlessly replicated
    // when a client connects
    scene_->CreateComponent<Octree>(LOCAL);

    // All static scene content and the camera are also created as local, so that they are unaffected by scene replication and are
    // not removed from the client upon connection. Create a Zone component first for ambient lighting & fog control.
    Node* zoneNode = scene_->CreateChild("Zone", LOCAL);
    Zone* zone = zoneNode->CreateComponent<Zone>();
    zone->SetBoundingBox(BoundingBox(-1000.0f, 1000.0f));
    zone->SetAmbientColor(Color(1.0f, 1.0f, 1.0f));
    zone->SetFogStart(100.0f);
    zone->SetFogEnd(300.0f);	

    // Create a "floor" consisting of several tiles. Make the tiles physical but leave small cracks between them
    Node* tableNode = scene_->CreateChild("Table", LOCAL);
	tableNode->SetPosition(Vector3(0.0f, 0.0f, 10.0f));
	tableNode->SetRotation(Quaternion(-90.0f, 0.0f, 0.0f));
	tableNode->SetScale(Vector3(DRAWING_TABLE_SIZE * PIXEL_SIZE, 1.0f, DRAWING_TABLE_SIZE * PIXEL_SIZE));
	StaticModel* screenObject = tableNode->CreateComponent<StaticModel>();
	screenObject->SetModel(cache->GetResource<Model>("Models/Plane.mdl"));

	// Create a texture
	tableTexture_ = SharedPtr<Texture2D>(new Texture2D(context_));
	tableTexture_->SetSize(DRAWING_TABLE_SIZE, DRAWING_TABLE_SIZE, Graphics::GetRGBFormat(), TEXTURE_DYNAMIC);
	tableTexture_->SetFilterMode(FILTER_NEAREST);
	char* c = new char[DRAWING_TABLE_SIZE * DRAWING_TABLE_SIZE * 3];
	for (int x = 0; x < DRAWING_TABLE_SIZE * 3; x += 3)
		for (int y = 0; y < DRAWING_TABLE_SIZE * 3; y += 3)
		{
			c[x + y * DRAWING_TABLE_SIZE] = 64;
			c[x + 1 + y * DRAWING_TABLE_SIZE] = 64;
			c[x + 2 + y * DRAWING_TABLE_SIZE] = 64;
		}
	tableTexture_->SetData(0, 0, 0, DRAWING_TABLE_SIZE, DRAWING_TABLE_SIZE, c);
	delete c;
	
	// Create a new material from scratch, use the diffuse unlit technique, assign the render texture
	// as its diffuse texture, then assign the material to the screen plane object
	SharedPtr<Material> renderMaterial(new Material(context_));
	renderMaterial->SetTechnique(0, cache->GetResource<Technique>("Techniques/DiffUnlit.xml"));
	renderMaterial->SetTexture(TU_DIFFUSE, tableTexture_);
	screenObject->SetMaterial(renderMaterial);

    // Create the camera. Limit far clip distance to match the fog
    // The camera needs to be created into a local node so that each client can retain its own camera, that is unaffected by
    // network messages. Furthermore, because the client removes all replicated scene nodes when connecting to a server scene,
    // the screen would become blank if the camera node was replicated (as only the locally created camera is assigned to a
    // viewport in SetupViewports() below)
	Graphics* graphics = GetSubsystem<Graphics>();

    cameraNode_ = scene_->CreateChild("Camera", LOCAL);
    Camera* camera = cameraNode_->CreateComponent<Camera>();
	camera->SetOrthographic(true);
	camera->SetOrthoSize((float)graphics->GetHeight() * PIXEL_SIZE);

    // Set an initial position for the camera scene node above the plane
    // cameraNode_->SetPosition(Vector3(0.0f, 0.0f, 0.0f));
}

void SceneReplication::CreateUI()
{
    ResourceCache* cache = GetSubsystem<ResourceCache>();
    UI* ui = GetSubsystem<UI>();
    UIElement* root = ui->GetRoot();
    XMLFile* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    // Set style to the UI root so that elements will inherit it
    root->SetDefaultStyle(uiStyle);

    // Create a Cursor UI element because we want to be able to hide and show it at will. When hidden, the mouse cursor will
    // control the camera, and when visible, it can interact with the login UI
    SharedPtr<Cursor> cursor(new Cursor(context_));
    cursor->SetStyleAuto(uiStyle);
    ui->SetCursor(cursor);
    // Set starting position of the cursor at the rendering window center
    Graphics* graphics = GetSubsystem<Graphics>();
    cursor->SetPosition(graphics->GetWidth() / 2, graphics->GetHeight() / 2);

    // Construct the instructions text element
    instructionsText_ = ui->GetRoot()->CreateChild<Text>();
    instructionsText_->SetText(
        "Click to draw a circle"
    );
    instructionsText_->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 15);
    // Position the text relative to the screen center
    instructionsText_->SetHorizontalAlignment(HA_CENTER);
    instructionsText_->SetVerticalAlignment(VA_CENTER);
    instructionsText_->SetPosition(0, graphics->GetHeight() / 4);
    // Hide until connected
    instructionsText_->SetVisible(false);

    buttonContainer_ = root->CreateChild<UIElement>();
    buttonContainer_->SetFixedSize(500, 20);
    buttonContainer_->SetPosition(20, 20);
    buttonContainer_->SetLayoutMode(LM_HORIZONTAL);

    textEdit_ = buttonContainer_->CreateChild<LineEdit>();
    textEdit_->SetStyleAuto();

    connectButton_ = CreateButton("Connect", 90);
    disconnectButton_ = CreateButton("Disconnect", 100);
    startServerButton_ = CreateButton("Start Server", 110);

    UpdateButtons();
}

void SceneReplication::SetupViewport()
{
    Renderer* renderer = GetSubsystem<Renderer>();

    // Set up a viewport to the Renderer subsystem so that the 3D scene can be seen
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void SceneReplication::SubscribeToEvents()
{
    // Subscribe HandlePostUpdate() method for processing update events. Subscribe to PostUpdate instead
    // of the usual Update so that physics simulation has already proceeded for the frame, and can
    // accurately follow the object with the camera
    SubscribeToEvent(E_POSTUPDATE, URHO3D_HANDLER(SceneReplication, HandlePostUpdate));

    // Subscribe to button actions
    SubscribeToEvent(connectButton_, E_RELEASED, URHO3D_HANDLER(SceneReplication, HandleConnect));
    SubscribeToEvent(disconnectButton_, E_RELEASED, URHO3D_HANDLER(SceneReplication, HandleDisconnect));
    SubscribeToEvent(startServerButton_, E_RELEASED, URHO3D_HANDLER(SceneReplication, HandleStartServer));

    // Subscribe to network events
    SubscribeToEvent(E_SERVERCONNECTED, URHO3D_HANDLER(SceneReplication, HandleConnectionStatus));
    SubscribeToEvent(E_SERVERDISCONNECTED, URHO3D_HANDLER(SceneReplication, HandleConnectionStatus));
    SubscribeToEvent(E_CONNECTFAILED, URHO3D_HANDLER(SceneReplication, HandleConnectionStatus));
    SubscribeToEvent(E_CLIENTCONNECTED, URHO3D_HANDLER(SceneReplication, HandleClientConnected));
    SubscribeToEvent(E_CLIENTDISCONNECTED, URHO3D_HANDLER(SceneReplication, HandleClientDisconnected));
    // This is a custom event, sent from the server to the client. It tells the node ID of the object the client should control
    SubscribeToEvent(E_CLIENTOBJECTID, URHO3D_HANDLER(SceneReplication, HandleClientObjectID));
	// This is a custom event, sent from the client to the server. It tells to the server where to draw circle
	SubscribeToEvent(E_DRAWCOMMAND_REQUEST, URHO3D_HANDLER(SceneReplication, HandleDrawCommandRequest));
	// This is a custom event, sent from the server to the client. It tells to the client where to draw circle
	SubscribeToEvent(E_DRAWCOMMAND_CONFIRM, URHO3D_HANDLER(SceneReplication, HandleDrawCommandConfirmed));

    // Events sent between client & server (remote events) must be explicitly registered or else they are not allowed to be received
    GetSubsystem<Network>()->RegisterRemoteEvent(E_CLIENTOBJECTID);
	GetSubsystem<Network>()->RegisterRemoteEvent(E_DRAWCOMMAND_REQUEST);
	GetSubsystem<Network>()->RegisterRemoteEvent(E_DRAWCOMMAND_CONFIRM);
}

Button* SceneReplication::CreateButton(const String& text, int width)
{
    ResourceCache* cache = GetSubsystem<ResourceCache>();
    Font* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    Button* button = buttonContainer_->CreateChild<Button>();
    button->SetStyleAuto();
    button->SetFixedWidth(width);

    Text* buttonText = button->CreateChild<Text>();
    buttonText->SetFont(font, 12);
    buttonText->SetAlignment(HA_CENTER, VA_CENTER);
    buttonText->SetText(text);

    return button;
}

void SceneReplication::UpdateButtons()
{
    Network* network = GetSubsystem<Network>();
    Connection* serverConnection = network->GetServerConnection();
    bool serverRunning = network->IsServerRunning();

    // Show and hide buttons so that eg. Connect and Disconnect are never shown at the same time
    connectButton_->SetVisible(!serverConnection && !serverRunning);
    disconnectButton_->SetVisible(serverConnection || serverRunning);
    startServerButton_->SetVisible(!serverConnection && !serverRunning);
    textEdit_->SetVisible(!serverConnection && !serverRunning);
}

Node* SceneReplication::CreateControllableObject()
{
    // Create the scene node & visual representation. This will be a replicated object
    Node* ballNode = scene_->CreateChild("Painter");

	// Create a random colored point light at the ball so that can see better where is going
	CirclePainter* painter = ballNode->CreateComponent<CirclePainter>();

	ResourceCache* cache = GetSubsystem<ResourceCache>();
	painter->SetColor(Color(Random(1.0f), Random(1.0f), Random(1.0f)));

    return ballNode;
}

void SceneReplication::CheckAuthority()
{
	Network* network = GetSubsystem<Network>();
	if (clientObjectAuth_ || !clientObjectID_ || network->IsServerRunning())
		return;

	Connection* serverConnection = network->GetServerConnection();
	if (!serverConnection)
		return;

	Node* node = scene_->GetNode(clientObjectID_);
	if (!node)
		return;

	CirclePainter* p = node->GetComponent<CirclePainter>();
	if (!p)
		return;
		
	p->TakeAuthority();

	clientObjectAuth_ = true;
	URHO3D_LOGINFO(Urho3D::ToString("Authority is taken"));
}

void SceneReplication::HandlePostUpdate(StringHash eventType, VariantMap& eventData)
{
	Input* input = GetSubsystem<Input>();
	if (input->GetKeyDown(KEY_F1))
		GetSubsystem<Console>()->Toggle();

	CheckAuthority();
}

void SceneReplication::HandleConnect(StringHash eventType, VariantMap& eventData)
{
    Network* network = GetSubsystem<Network>();
    String address = textEdit_->GetText().Trimmed();
    if (address.Empty())
        address = "localhost"; // Use localhost to connect if nothing else specified

    // Connect to server, specify scene to use as a client for replication
    clientObjectID_ = 0; // Reset own object ID from possible previous connection
    network->Connect(address, SERVER_PORT, scene_);

    UpdateButtons();
}

void SceneReplication::HandleDisconnect(StringHash eventType, VariantMap& eventData)
{
    Network* network = GetSubsystem<Network>();
    Connection* serverConnection = network->GetServerConnection();
    // If we were connected to server, disconnect. Or if we were running a server, stop it. In both cases clear the
    // scene of all replicated content, but let the local nodes & components (the static world + camera) stay
    if (serverConnection)
    {
        serverConnection->Disconnect();
        scene_->Clear(true, false);
        clientObjectID_ = 0;
		clientObjectAuth_ = false;
    }
    // Or if we were running a server, stop it
    else if (network->IsServerRunning())
    {
        network->StopServer();
        scene_->Clear(true, false);
    }

    UpdateButtons();
}

void SceneReplication::HandleStartServer(StringHash eventType, VariantMap& eventData)
{
    Network* network = GetSubsystem<Network>();
    network->StartServer(SERVER_PORT);

    UpdateButtons();
}

void SceneReplication::HandleConnectionStatus(StringHash eventType, VariantMap& eventData)
{
    UpdateButtons();
}

void SceneReplication::HandleClientConnected(StringHash eventType, VariantMap& eventData)
{
    using namespace ClientConnected;

    // When a client connects, assign to scene to begin scene replication
    Connection* newConnection = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    newConnection->SetScene(scene_);

    // Then create a controllable object for that client
    Node* newObject = CreateControllableObject();
    serverObjects_[newConnection] = newObject;

    // Finally send the object's node ID using a remote event
    VariantMap remoteEventData;
    remoteEventData[P_ID] = newObject->GetID();
    newConnection->SendRemoteEvent(E_CLIENTOBJECTID, true, remoteEventData);

	for (auto it = history.Begin(); it != history.End(); ++it)
	{
		VariantMap remoteEventData;
		remoteEventData[P_DC_POSITION] = it->position;
		remoteEventData[P_DC_COLOR] = it->color;
		newConnection->SendRemoteEvent(E_DRAWCOMMAND_CONFIRM, true, remoteEventData);
	}
}

void SceneReplication::HandleClientDisconnected(StringHash eventType, VariantMap& eventData)
{
    using namespace ClientConnected;

    // When a client disconnects, remove the controlled object
    Connection* connection = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    Node* object = serverObjects_[connection];
    if (object)
        object->Remove();

    serverObjects_.Erase(connection);
}

void SceneReplication::HandleClientObjectID(StringHash eventType, VariantMap& eventData)
{
    clientObjectID_ = eventData[P_ID].GetUInt();
}

void SceneReplication::DrawCircle(const Vector2& drawAt, const Color& color)
{
	const int diameter = 10;

	float readlbounds = (DRAWING_TABLE_SIZE)* PIXEL_SIZE / 2.0f;

	IntVector2 coord = IntVector2((drawAt.x_ + readlbounds) / PIXEL_SIZE, DRAWING_TABLE_SIZE - (drawAt.y_ + readlbounds) / PIXEL_SIZE);

	if (coord.x_ <= 0 || coord.x_ > DRAWING_TABLE_SIZE || coord.y_ <= 0 || coord.y_ > DRAWING_TABLE_SIZE)
		return;

	char* data = new char[diameter * 3];
	for (int i = 0; i < diameter * 3; i += 3)
	{
		data[i] = color.r_ * 255;
		data[i + 1] = color.g_ * 255;
		data[i + 2] = color.b_ * 255;
	}

	int r = diameter / 2;
	{
		int x1;
		int x2;
		int counter = (coord.y_ + r);
		for (int line = (coord.y_ - r); line <= counter; line++)
		{
			x1 = int(coord.x_ + sqrt((r*r) - ((line - coord.y_)*(line - coord.y_))) + 0.5);
			x2 = int(coord.x_ - sqrt((r*r) - ((line - coord.y_)*(line - coord.y_))) + 0.5);
			x1 = Clamp(x1, 0, DRAWING_TABLE_SIZE);
			x2 = Clamp(x2, 0, DRAWING_TABLE_SIZE);
			if (x1 <= x2)
				continue;
			URHO3D_LOGDEBUG(Urho3D::ToString("SetData(%d, %d, %d, %d, %d)", 0, x2, line, x1 - x2, 1));
			tableTexture_->SetData(0, x2, line, x1 - x2, 1, data);
		}
	}
	delete data;
}

void SceneReplication::HandleDrawCommandRequest(StringHash eventType, VariantMap& eventData)
{
	unsigned int	drawBy = eventData[P_ID].GetUInt();
	Vector2		drawAt = eventData[P_DC_POSITION].GetVector2();

	Node* node = scene_->GetNode(drawBy);
	if (!node)
		return;

	CirclePainter* p = node->GetComponent<CirclePainter>();
	if (!p)
		return;

	DrawCommand dc = DrawCommand(drawAt, p->GetColor());
	history.Push(dc);
	DrawCircle(dc.position, dc.color);


	Network* network = GetSubsystem<Network>();
	if (!network->IsServerRunning())
		return;

	VariantMap remoteEventData;
	remoteEventData[P_DC_POSITION] = dc.position;
	remoteEventData[P_DC_COLOR] = dc.color;
	network->BroadcastRemoteEvent(E_DRAWCOMMAND_CONFIRM, true, remoteEventData);
}

void SceneReplication::HandleDrawCommandConfirmed(StringHash eventType, VariantMap& eventData)
{
	Vector2		drawAt = eventData[P_DC_POSITION].GetVector2();
	Color		color = eventData[P_DC_COLOR].GetColor();

	DrawCircle(drawAt, color);
}