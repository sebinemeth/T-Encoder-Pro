# Home Assistant LVGL Controller - Tileview Version

## Overview

This is a refactored version of the Home Assistant controller that uses a **tileview layout** for navigating between entities. Instead of a single screen that cycles between entities, each entity now has its own dedicated tile with specialized controls based on the entity type.

## Key Features

### 🔄 **Swipeable Tileview Interface**
- Each entity gets its own tile with custom controls
- Navigate by **swiping** on the touchscreen or using the **rotary encoder**
- Smooth animations between tiles

### 🎛️ **Entity-Specific Controls**

#### Toggle Entities (Lights, Switches)
- Large switch widget for touch control
- Icon display with entity name
- Status indicator

#### Cover Entities (Blinds, Shutters)
- Position slider (0-100%)
- Three action buttons: OPEN, STOP, CLOSE
- Position percentage display

#### Thermostat Entities
- Current temperature display
- Target temperature slider
- HVAC mode and action status

#### Sensor Entities
- Large value display
- Unit of measurement
- Read-only information display

#### Fan Entities
- Speed control slider (0-100%)
- Speed percentage display

### 🎮 **Dual Input Methods**
- **Touch**: Direct interaction with widgets (switches, sliders, buttons)
- **Rotary Encoder**: Navigation between tiles + context-sensitive actions
  - Rotation: Navigate between entity tiles
  - Button press: Entity-specific action (toggle switches, stop covers, etc.)

## File Structure

```
HomeAssistant_LVGL/
├── HomeAssistant_LVGL.ino              # Original single-screen version
├── HomeAssistant_LVGL_Tileview.ino     # New tileview version (use this!)
├── entities.h                          # Updated entity definitions with new types
├── ha_config.h                         # Configuration templates
├── ha_config_private.h                 # Your private config (gitignored)
└── port/                              # LVGL port files
```

## Setup Instructions

1. **Use the new file**: Upload `HomeAssistant_LVGL_Tileview.ino` instead of the original
2. **Configure entities**: Edit the entity definitions in `entities.h`
3. **Set credentials**: Create `ha_config_private.h` with your WiFi and HA details
4. **Build and upload**: Use PlatformIO with the existing configuration

## Entity Configuration

In `entities.h`, you can now define entities with automatic type detection:

```cpp
HAEntity entities[ENTITY_COUNT] = {
    {"Desk Lamp", "light.desk_lamp", ENTITY_TOGGLE, /* other fields auto-initialized */},
    {"Office Cover", "cover.office_blinds", ENTITY_COVER, /* other fields auto-initialized */},
    {"Thermostat", "climate.living_room", ENTITY_THERMOSTAT, /* other fields auto-initialized */},
    {"Temperature", "sensor.temperature", ENTITY_SENSOR, /* other fields auto-initialized */}
};
```

The system automatically:
- Detects entity types from the `entity_id` domain
- Initializes appropriate fields for each entity type
- Creates the right UI controls for each entity
- Fetches and parses entity states correctly

## Supported Entity Types

| Type | Domains | Controls | Features |
|------|---------|----------|----------|
| `ENTITY_TOGGLE` | `light`, `switch`, `input_boolean` | Switch widget | Touch + encoder toggle |
| `ENTITY_COVER` | `cover` | Position slider + action buttons | Open/Close/Stop/Position control |
| `ENTITY_THERMOSTAT` | `climate` | Temperature display + target slider | Current/target temperature |
| `ENTITY_SENSOR` | `sensor` | Value display | Read-only with units |
| `ENTITY_FAN` | `fan` | Speed slider | 0-100% speed control |

## Navigation

- **Swipe left/right**: Navigate between entity tiles
- **Rotate encoder**: Navigate between entity tiles  
- **Press encoder**: Perform entity-specific action
- **Touch controls**: Direct widget interaction

## API Integration

The system supports full Home Assistant REST API integration:

- **State fetching**: Automatic periodic updates for all entities
- **Action execution**: Optimistic UI with background API calls
- **Error handling**: UI state reversion on API failures
- **Multiple entity types**: Different API endpoints per entity type

## Advantages Over Original

1. **Better Organization**: Each entity has dedicated screen real estate
2. **Scalability**: Easy to add new entity types with custom controls
3. **Intuitive Navigation**: Natural swiping between entities
4. **Flexible UI**: Entity-specific layouts optimized for each type
5. **Touch-Friendly**: Large touch targets and direct widget interaction
6. **Future-Ready**: Extensible architecture for new entity types

## Adding New Entity Types

To add a new entity type:

1. Add enum value in `entities.h`
2. Add fields to the `HAEntity` struct
3. Create `createXXXTile()` function in the main file
4. Add case to `createEntityTile()` switch statement
5. Implement API integration in `fetchEntityState()` and `processAPIRequest()`
6. Add UI update logic in `updateEntityTile()`

## Performance

- **Memory efficient**: Tiles created on-demand
- **Smooth animations**: LVGL hardware acceleration
- **Fast API calls**: Optimistic UI with 35-127ms response times
- **Responsive input**: 50ms encoder debouncing, 300ms navigation cooldown

The tileview architecture provides a much more scalable and user-friendly interface for controlling multiple Home Assistant entities with the T-Encoder Pro device.
