#include "FingerSaber.hpp"

#include "ModConfig.hpp"

#include "GlobalNamespace/OVRInput.hpp"
#include "GlobalNamespace/OVRManager.hpp"

#include "UnityEngine/Transform.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Component.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/HideFlags.hpp"
#include "UnityEngine/Resources.hpp"
#include "UnityEngine/PrimitiveType.hpp"

#include "GlobalNamespace/ColorScheme.hpp"
#include "GlobalNamespace/ColorManager.hpp"
#include "GlobalNamespace/ColorManagerInstaller.hpp"
#include "GlobalNamespace/ColorSchemeSO.hpp"
#include "GlobalNamespace/PlayerData.hpp"

#include "GlobalNamespace/SaberType.hpp"
#include "GlobalNamespace/ColorSchemesSettings.hpp"
#include "System/Collections/Generic/Dictionary_2.hpp"

#include <sstream>
#include <string>
#include <vector>

#include "GlobalNamespace/OVRPlugin.hpp"
#include "GlobalNamespace/OVRHand.hpp"

#include "GlobalNamespace/FirstPersonFlyingController.hpp"
#include "GlobalNamespace/VRController.hpp"
#include "GlobalNamespace/OculusVRHelper.hpp"
#include "GlobalNamespace/VRPlatformSDK.hpp"
#include "GlobalNamespace/OVRCameraRig.hpp"

#include "GlobalNamespace/OVRSkeleton.hpp"
#include "GlobalNamespace/OVRSkeletonRenderer.hpp"
#include "GlobalNamespace/OVRBone.hpp"

#include "UnityEngine/EventSystems/PointerInputModule.hpp"
#include "VRUIControls/VRInputModule.hpp"
#include "GlobalNamespace/TimeHelper.hpp"

#include "UnityEngine/Camera.hpp"

#include "System/Collections/Generic/List_1.hpp"
#include "System/Collections/Generic/IList_1.hpp"
#include "System/Collections/ObjectModel/ReadOnlyCollection_1.hpp"

#include "UnityEngine/LineRenderer.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/Color.hpp"

#include "UnityEngine/Resources.hpp"

#include "UnityEngine/RenderSettings.hpp"
#include "UnityEngine/Shader.hpp"

#include "logging.hpp"

#include "metacore/shared/game.hpp"

FingerSaber modManager;
extern modloader::ModInfo modInfo;

void FingerSaber::InstallHooks()
{
    _Hook_SceneManager_SetActiveScene();
    _Hook_MultiplayerSpectatorController_SwitchToSpectatingSpot();
    _Hook_SaberModelController_Init();
    _Hook_GamePause_Pause();
    _Hook_GamePause_WillResume();
    _Hook_MultiplayerLocalActivePlayerGameplayManager_PerformPlayerFail();
    _Hook_BoneVisualization_Update();

    _Hook_menu_saber_functionality();
}

const UnityEngine::Color defaultRightColor{0.156863, 0.556863, 0.823529, 1.000000};
const UnityEngine::Color defaultLeftColor{0.784314, 0.078431, 0.078431, 1.000000};

void FingerSaber::update_scoreSubmission()
{
    MetaCore::Game::SetScoreSubmission(modInfo.id, getModConfig().ModEnabled.GetValue());
}

void FingerSaber::_Destroy_OculusHands()
{
    auto old_HandTracking_container = UnityEngine::GameObject::Find("HandTracking_container");
    if (old_HandTracking_container)
    {
        UnityEngine::GameObject::Destroy(old_HandTracking_container);
    }

    // Regardless if destroy was initiated or not, we can assure that the handTrackingObject is nullptr,
    // as HandTracking_container is created with DontDestroyOnLoad option -> If they exists they must either exists in scene.
    handTrackingObjectsParent = nullptr;
    rightOVRHand = nullptr;
    leftOVRHand = nullptr;
    rightOVRSkeleton = nullptr;
    leftOVRSkeleton = nullptr;
    rightOVRSkeletonRenderer = nullptr;
    leftOVRSkeletonRenderer = nullptr;
}

bool FingerSaber::shouldInitializeHandsForScene(const std::string &sceneName) const
{
    return sceneName == "MainMenu" || sceneName == "GameCore";
}

void FingerSaber::_InitializeOculusHands()
{
    INFO("Oculus Hand MENU Initialization ..");

    // Only create the material if it did not exist yet
    if (leftHandSkeletonMat == nullptr || rightHandSkeletonMat == nullptr)
    {
        createNewSkeletonMaterials();
    }

    const bool hasSkeletonMaterials = leftHandSkeletonMat != nullptr && rightHandSkeletonMat != nullptr;
    if (!hasSkeletonMaterials)
    {
        INFO("Skeleton materials unavailable. Hand tracking will initialize without skeleton renderer materials.");
    }

    this->_Destroy_OculusHands();

    handTrackingObjectsParent = UnityEngine::GameObject::New_ctor(("HandTracking_container"));
    UnityEngine::GameObject::DontDestroyOnLoad(handTrackingObjectsParent);

    // ---

    auto rightHandAnchor = UnityEngine::GameObject::New_ctor(("rightHandAnchor"));
    rightHandAnchor->get_transform()->set_parent(handTrackingObjectsParent->get_transform());

    auto rightHandTrackingGo = UnityEngine::GameObject::New_ctor(("rightHandTracking"));
    rightHandTrackingGo->get_transform()->set_parent(rightHandAnchor->get_transform());

    rightOVRHand = rightHandTrackingGo->AddComponent<GlobalNamespace::OVRHand *>();
    rightOVRHand->HandType = GlobalNamespace::OVRHand::Hand::HandRight;

    rightOVRSkeleton = rightHandTrackingGo->AddComponent<GlobalNamespace::OVRSkeleton *>();
    rightOVRSkeleton->_skeletonType = GlobalNamespace::OVRSkeleton::SkeletonType::HandRight;
    rightOVRSkeleton->_updateRootPose = true;
    rightOVRSkeleton->Awake();
    rightOVRSkeleton->Initialize();

    this->rightOVRSkeletonRenderer = rightHandTrackingGo->AddComponent<GlobalNamespace::OVRSkeletonRenderer *>();
    if (rightOVRSkeletonRenderer)
    {
        if (rightHandSkeletonMat)
        {
            rightHandSkeletonMat->SetColor("_Color", defaultRightColor);
            rightOVRSkeletonRenderer->_skeletonMaterial = rightHandSkeletonMat;
            rightOVRSkeletonRenderer->_systemGestureMaterial = rightHandSkeletonMat;
            INFO("Right skeleton renderer material assigned");
        }
        else
        {
            INFO("Right skeleton material missing; skipping custom renderer material assignment");
        }
        rightOVRSkeletonRenderer->Initialize();
    }
    else
    {
        INFO("Failed to add right OVRSkeletonRenderer component");
    }

    INFO("Right Handtracking stuff initialized");

    // ---

    UnityEngine::GameObject *leftHandAnchor = UnityEngine::GameObject::New_ctor(("leftHandAnchor"));
    leftHandAnchor->get_transform()->set_parent(handTrackingObjectsParent->get_transform());

    auto leftHandTrackingGo = UnityEngine::GameObject::New_ctor(("leftHandTracking"));
    leftHandTrackingGo->get_transform()->set_parent(leftHandAnchor->get_transform());

    leftOVRHand = leftHandTrackingGo->AddComponent<GlobalNamespace::OVRHand *>();
    leftOVRHand->HandType = GlobalNamespace::OVRHand::Hand::HandLeft;

    leftOVRSkeleton = leftHandTrackingGo->AddComponent<GlobalNamespace::OVRSkeleton *>();
    leftOVRSkeleton->_skeletonType = GlobalNamespace::OVRSkeleton::SkeletonType::HandLeft;
    leftOVRSkeleton->_updateRootPose = true;
    leftOVRSkeleton->Awake();
    leftOVRSkeleton->Initialize();

    this->leftOVRSkeletonRenderer = leftHandTrackingGo->AddComponent<GlobalNamespace::OVRSkeletonRenderer *>();
    if (leftOVRSkeletonRenderer)
    {
        if (leftHandSkeletonMat)
        {
            leftHandSkeletonMat->SetColor(("_Color"), defaultLeftColor);
            leftOVRSkeletonRenderer->_skeletonMaterial = leftHandSkeletonMat;
            leftOVRSkeletonRenderer->_systemGestureMaterial = leftHandSkeletonMat;
            INFO("Left skeleton renderer material assigned");
        }
        else
        {
            INFO("Left skeleton material missing; skipping custom renderer material assignment");
        }
        leftOVRSkeletonRenderer->Initialize();
    }
    else
    {
        INFO("Failed to add left OVRSkeletonRenderer component");
    }

    INFO("Left Handtracking stuff initialized");
}

void FingerSaber::createNewSkeletonMaterials()
{
    UnityEngine::Shader *shaderToUse = nullptr;
    std::vector<std::string> shaderCandidates = {"Custom/SimpleLit", "BeatSaber/UnlitGlow", "Hidden/Internal-Colored", "Standard"};

    for (auto const &shaderName : shaderCandidates)
    {
        std::optional<UnityEngine::Shader *> shader =
            UnityEngine::Resources::FindObjectsOfTypeAll<UnityEngine::Shader *>().front([&](auto const &e)
                                                                                        { return e && e->get_name() == shaderName; });
        if (shader.has_value())
        {
            shaderToUse = shader.value();
            INFO("Skeleton shader found: {}", shaderName);
            break;
        }
        INFO("Skeleton shader candidate not found: {}", shaderName);
    }

    if (!shaderToUse)
    {
        INFO("No suitable shader found for skeleton materials; skipping material creation");
        return;
    }

    leftHandSkeletonMat = UnityEngine::Material::New_ctor(shaderToUse);
    rightHandSkeletonMat = UnityEngine::Material::New_ctor(shaderToUse);

    if (!leftHandSkeletonMat || !rightHandSkeletonMat)
    {
        INFO("Failed to create one or more skeleton materials (left: {}, right: {})", leftHandSkeletonMat != nullptr, rightHandSkeletonMat != nullptr);
        leftHandSkeletonMat = nullptr;
        rightHandSkeletonMat = nullptr;
        return;
    }

    INFO("Skeleton materials created successfully");
}

void FingerSaber::ChangeRightSkeletonRendererColor(UnityEngine::Color col)
{
    if (rightHandSkeletonMat)
        rightHandSkeletonMat->SetColor(("_Color"), col);
}
void FingerSaber::ChangeLeftSkeletonRendererColor(UnityEngine::Color col)
{
    if (leftHandSkeletonMat)
        leftHandSkeletonMat->SetColor(("_Color"), col);
}

void FingerSaber::update_LRHandIsTracked()
{
    if (!rightOVRHand || !leftOVRHand)
    {
        _oculusRHandIsTracked = false;
        _oculusLHandIsTracked = false;
        return;
    }
    _oculusRHandIsTracked = rightOVRHand->IsTracked;
    _oculusLHandIsTracked = leftOVRHand->IsTracked;
}
void FingerSaber::update_LRHandClickRequested()
{
    _rHandClickRequested = GlobalNamespace::OVRInput::Get(GlobalNamespace::OVRInput::Button::One, GlobalNamespace::OVRInput::Controller::RHand)
                               ? (_oculusRHandIsTracked)
                               : false;
    _lHandClickRequested = GlobalNamespace::OVRInput::Get(GlobalNamespace::OVRInput::Button::One, GlobalNamespace::OVRInput::Controller::LHand)
                               ? (_oculusLHandIsTracked)
                               : false;
}

void FingerSaber::disable_skeletonRender_lines(GlobalNamespace::OVRSkeletonRenderer *skeletonRenderer)
{
    if (skeletonRenderer == nullptr)
        return;

    for (int i = 0; i < skeletonRenderer->_boneVisualizations->Count; i++)
    {
        auto line = skeletonRenderer->_boneVisualizations->get_Item(i)->Line;
        line->enabled = false;
    }
}

void FingerSaber::update_LRTargetBone()
{
    /** -----  https://developer.oculus.com/documentation/unity/unity-handtracking/  -----
     * Hand_ThumbTip    = Hand_Start + Hand_MaxSkinnable + 0 // tip of the thumb
     * Hand_IndexTip    = Hand_Start + Hand_MaxSkinnable + 1 // tip of the index finger
     * Hand_MiddleTip   = Hand_Start + Hand_MaxSkinnable + 2 // tip of the middle finger
     * Hand_RingTip     = Hand_Start + Hand_MaxSkinnable + 3 // tip of the ring finger
     * Hand_PinkyTip    = Hand_Start + Hand_MaxSkinnable + 4 // tip of the pinky
     * */
    int tipStart = (int)GlobalNamespace::OVRSkeleton::BoneId::Hand_Start + (int)GlobalNamespace::OVRSkeleton::BoneId::Hand_MaxSkinnable;

    this->leftTargetBone = tipStart + (getModConfig().LeftHandTargetIdx.GetValue() % 5);
    this->rightTargetBone = tipStart + (getModConfig().RightHandTargetIdx.GetValue() % 5);

    leftHand_isTargetHandRight = getModConfig().LeftHandTargetIdx.GetValue() >= 5;
    rightHand_isTargetHandLeft = getModConfig().RightHandTargetIdx.GetValue() >= 5;
}
