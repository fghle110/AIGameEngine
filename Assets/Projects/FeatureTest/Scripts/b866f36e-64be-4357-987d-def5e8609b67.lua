-- Rotates the entity and gently bobs it up and down.
-- Exercises engine.time, engine.properties and entity transforms.
local entity = nil
local elapsed = 0.0

function OnStart(self)
    entity = self
    elapsed = 0.0
    engine.log.info('Spinner started on ' .. entity:name())
end

function OnUpdate(self, deltaTime)
    elapsed = elapsed + deltaTime
    local degrees = elapsed * (engine.properties.speed or 45.0)
    local height = (engine.properties.bobHeight or 0.25) * math.sin(elapsed * 2.0)
    entity:setRotation({ x = 0.0, y = degrees, z = 0.0 })
    local position = entity:position()
    position.y = position.y + height
    entity:setPosition(position)
end

function OnDestroy(self)
    engine.log.info('Spinner destroyed on ' .. self:name())
end
