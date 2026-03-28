#include "Character.hpp"
#include "Battle.hpp"
#include "BattleLog.hpp"
#include "Skill.hpp"
#include "StatusEffect.hpp"
#include "Buff.hpp"
#include "Party.hpp"
#include "StatModifier.hpp"
#include <algorithm>

Character::Character(std::string charName, int charLevel, StatBlock stats, 
              int startWeight, int weightLimit)
    : name(std::move(charName)), level(charLevel), baseStats(stats),
      hp(stats.getMaxHP()), mana(0, 100), weight(startWeight), 
      maxWeight(weightLimit), defending(false), ownerParty(nullptr) {}

const std::string& Character::getName() const {
    return name;
}

int Character::getHP() const {
    return hp;
}

int Character::getMaxHP() const {// Returns maximum HP after applying modifiers
    return getFinalStat(StatType::MaxHP);
}

int Character::getLevel() const {
    return level;
}

int Character::getWeight() const {
    return weight;
}

int Character::getMaxWeight() const {
    return maxWeight;
}

int Character::getMana() const {
    return mana.getCurrent();
}

int Character::getMaxMana() const {
    return mana.getMax();
}

bool Character::isDefending() const {// Returns whether the character is defending
    return defending;
}

Party* Character::getOwnerParty() const {// Returns the party the character belongs to
    return ownerParty;
}

const std::vector<std::shared_ptr<Skill>>& Character::getSkills() const {
    return skills;
}

const StatBlock& Character::getBaseStats() const {// Returns base stats (before buffs/debuffs)
    return baseStats;
}

bool Character::isAlive() const {
    return hp > 0;
}

bool Character::isOverencumbered() const {// Checks if character is carrying too much weight
    return weight > maxWeight;
}

void Character::setOwnerParty(Party* party) {
    ownerParty = party;
}

void Character::setDefending(bool def) {
    defending = def;
}

void Character::setLevel(int lvl) {// Sets character level (minimum level = 1)
    level = std::max(1, lvl);
}
// Adds weight (e.g. picking up items)
void Character::addWeight(int amount) {
    weight += amount;
    checkOverencumbered();
}

void Character::removeWeight(int amount) {// Removes weight (e.g. dropping items)
    weight = std::max(0, weight - amount);
}

void Character::increaseAttack(int amount) {
    baseStats.increaseStat(StatType::Attack, amount);
}

void Character::takeDamage(int amount) {// Applies damage to the character
    hp = std::max(0, hp - std::max(0, amount));
}

void Character::heal(int amount) {
    hp = std::min(getMaxHP(), hp + std::max(0, amount));
}

void Character::consumeMana(int amount) {// Consumes mana when casting skills
    mana.consume(amount);
}

void Character::restoreMana(int amount) {// Restores mana
    mana.restore(amount);
}

void Character::addSkill(std::shared_ptr<Skill> s) {
    skills.push_back(std::move(s));
}

void Character::removeSkill(const std::shared_ptr<Skill>& s) {
    skills.erase(std::remove(skills.begin(), skills.end(), s), skills.end());
}
// Applies a status effect to the character (e.g. poison, stun)
void Character::applyStatus(Battle& battle, std::shared_ptr<StatusEffect> status) {
    if (!status) return;
    
    statuses.push_back(status);
    battle.getLog()->add("  " + name + " gains status: " + status->getName());
    status->onApply(battle, shared_from_this());
}

void Character::applyBuff(Battle& battle, std::shared_ptr<Buff> buff) {
    if (!buff) return;
    
    buffs.push_back(buff);
    battle.getLog()->add("  " + name + " gains buff: " + buff->getName());
}
// Updates all status effects each turn
void Character::tickStatuses(Battle& battle, TickTiming when) {
    for (auto& status : statuses) {
        if (status && status->getTiming() == when) {// Check if status should trigger at this timing
            status->onTick(battle, shared_from_this());// Execute status behavior
            status->decrement();// Reduce remaining duration
            
            if (status->expired()) {
                battle.getLog()->add("  " + name + "'s " + status->getName() + " expired");
                status->onExpire(battle, shared_from_this()); // Execute expire logic
            }
        }
    }

    statuses.erase( // Remove expired statuses from list
        std::remove_if(statuses.begin(), statuses.end(),
            [](const std::shared_ptr<StatusEffect>& s) { return !s || s->expired(); }),
        statuses.end()
    );
}
// Updates buffs every turn
void Character::tickBuffs(Battle& battle) {
    for (auto& buff : buffs) {
        if (buff) {
            buff->decrementDuration();
            
            if (buff->isExpired()) {
                battle.getLog()->add("  " + name + "'s " + buff->getName() + " expired");
            }
        }
    }

    buffs.erase(
        std::remove_if(buffs.begin(), buffs.end(),
            [](const std::shared_ptr<Buff>& b) { return !b || b->isExpired(); }),
        buffs.end()
    );
}

void Character::tickCooldowns() {
    for (auto& skill : skills) {
        if (skill) {
            skill->decrementCooldown();
        }
    }
}

bool Character::canAct() const {
    for (const auto& status : statuses) {
        if (status && status->blocksAction()) {
            return false;
        }
    }
    return true;
}

void Character::checkOverencumbered() {
    // This is called automatically when weight changes
}

int Character::getFinalStat(StatType type) const {
    int base = baseStats.getStat(type);

    int flatSum = 0;
    float mult = 1.0f;

    // Apply status modifiers
    for (const auto& status : statuses) {
        if (!status) continue;
        
        for (const auto& mod : status->modifiers()) {
            if (mod.getType() == type) {
                flatSum += mod.getFlat();
                mult *= mod.getMult();
            }
        }
    }

    int val = static_cast<int>((base + flatSum) * mult);
    return std::max(0, val);
}

int Character::getModifiedAttack() const {
    int baseAttack = getFinalStat(StatType::Attack);
    
    // Apply attack buffs - identified by name since new Buff uses name-based dispatch
    for (const auto& buff : buffs) {
        if (buff && buff->getName() == "StrengthUp") {
            baseAttack += 10; // magnitude mirrors applyEffect() hardcoded value in Buff.cpp
        }
    }
    
    return baseAttack;
}

int Character::getModifiedDefense() const {
    int baseDefense = getFinalStat(StatType::Defense);
    
    // Apply defense buffs - identified by name since new Buff uses name-based dispatch
    for (const auto& buff : buffs) {
        if (buff && buff->getName() == "DefenseUp") {
            baseDefense += 10; // magnitude mirrors applyEffect() hardcoded value in Buff.cpp
        }
    }
    
    return baseDefense;
}

void Character::applyNerf(int percentage, Battle& battle) {
    int damage = (hp * percentage) / 100;
    takeDamage(damage);
    
    battle.getLog()->add("  " + name + " is nerfed! Lost " + 
                        std::to_string(damage) + " HP (" + 
                        std::to_string(percentage) + "% of current health)");
}
