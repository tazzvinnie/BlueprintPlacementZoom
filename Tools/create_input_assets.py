# Creates the Enhanced Input assets this mod uses. Run headless from the SML project:
#   UnrealEditor-Cmd.exe C:\SF\SML\FactoryGame.uproject -run=pythonscript -script="<this file>"
# Re-running updates the existing assets in place.
import unreal

ROOT = "/BlueprintPlacementZoom/Input"
PARENT_CONTEXT = "/Game/FactoryGame/Inputs/Equipment/Buildgun/Build/MC_BuildGunBuild"

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary


def get_or_create(name, cls):
    path = f"{ROOT}/{name}"
    if lib.does_asset_exist(path):
        asset = lib.load_asset(path)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", cls)
        asset = tools.create_asset(name, ROOT, cls, factory)
    if asset is None:
        raise RuntimeError(f"Could not create {path}")
    return asset


def make_key(name):
    key = unreal.Key()
    key.set_editor_property("key_name", name)
    return key


def make_mapping(context, action, key_name, triggers=(), mappable_name=None, display_name=None):
    mapping = unreal.EnhancedActionKeyMapping()
    mapping.set_editor_property("action", action)
    mapping.set_editor_property("key", make_key(key_name))
    mapping.set_editor_property("triggers", list(triggers))
    if mappable_name:
        settings = unreal.new_object(unreal.PlayerMappableKeySettings, outer=context)
        settings.set_editor_property("name", mappable_name)
        settings.set_editor_property("display_name", display_name)
        mapping.set_editor_property("setting_behavior", unreal.PlayerMappableKeySettingBehaviors.OVERRIDE_SETTINGS)
        mapping.set_editor_property("player_mappable_key_settings", settings)
    else:
        mapping.set_editor_property("setting_behavior", unreal.PlayerMappableKeySettingBehaviors.IGNORE_SETTINGS)
    return mapping


toggle = get_or_create("IA_BPZ_ToggleZoom", unreal.InputAction)
toggle.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)
toggle.set_editor_property("action_description", "Toggle Blueprint Zoom")

modifier = get_or_create("IA_BPZ_ZoomModifier", unreal.InputAction)
modifier.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)
modifier.set_editor_property("action_description", "Blueprint Zoom Modifier")

scroll = get_or_create("IA_BPZ_ZoomScroll", unreal.InputAction)
scroll.set_editor_property("value_type", unreal.InputActionValueType.AXIS1D)
scroll.set_editor_property("action_description", "Blueprint Zoom Scroll")

# Rebindable keys, shown under Options -> Controls. Bound automatically with the build gun's build context.
keybinds = get_or_create("MC_BPZ_Keybinds", unreal.FGChildInputMappingContext)
keybinds.set_editor_property("m_display_name", "Blueprint Zoomer")
keybinds.set_editor_property("m_parent_context", unreal.load_asset(PARENT_CONTEXT))
pressed = unreal.new_object(unreal.InputTriggerPressed, outer=keybinds)
keybinds.set_editor_property("mappings", [
    make_mapping(keybinds, toggle, "F7", [pressed], "BPZ.ToggleZoom", "Toggle Blueprint Zoom"),
    make_mapping(keybinds, modifier, "LeftAlt", [], "BPZ.ZoomModifier", "Adjust Blueprint Zoom (hold + scroll)"),
])

# Not rebindable. The mod binds this at high priority only while placing a blueprint with the
# modifier held; the action consumes the wheel, so the game's own scroll mappings (hologram
# rotation) don't also fire.
scroll_context = get_or_create("MC_BPZ_Scroll", unreal.InputMappingContext)
scroll.set_editor_property("consume_input", True)
scroll_context.set_editor_property("mappings", [
    make_mapping(scroll_context, scroll, "MouseWheelAxis"),
])

for asset in (toggle, modifier, scroll, keybinds, scroll_context):
    if not lib.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save {asset.get_path_name()}")
    unreal.log(f"BPZ: saved {asset.get_path_name()}")
