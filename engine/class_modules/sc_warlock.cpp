#include "simulationcraft.hpp"
#include "action/parse_effects.hpp"

#include "class_modules/apl/warlock.hpp"

namespace warlock
{
struct warlock_t;

struct warlock_td_t final : public actor_target_data_t
{
  struct dots_t
  {
    dot_t* bane_of_agony = nullptr;
    dot_t* corruption = nullptr;
    dot_t* immolate = nullptr;
  } dots;

  warlock_td_t( player_t* target, warlock_t& p );
};

struct warlock_t final : public parse_player_effects_t
{
  warlock_t( sim_t* sim, util::string_view name, race_e r = RACE_NONE ) :
    parse_player_effects_t( sim, WARLOCK, name, r ) {}

  resource_e primary_resource() const override { return RESOURCE_MANA; }

  warlock_td_t* get_target_data( player_t* target ) const override;

  std::string default_potion() const override   { return warlock_apl::potion( this ); }
  std::string default_flask() const override    { return warlock_apl::flask( this ); }
  std::string default_food() const override     { return warlock_apl::food( this ); }

  action_t* create_action( util::string_view name, util::string_view options_str ) override;
  void init_action_list() override;
  void init_base_stats() override;
  void init_spells() override;

  void create_pets() override;

  struct pets_t
  {
    pet_t* imp = nullptr;
  } pets;

  struct
  {
    spell_ranks_t bane_of_agony;
    spell_ranks_t corruption;
    spell_ranks_t curse_of_the_elements;
    spell_ranks_t drain_life;
    spell_ranks_t firebolt;
    spell_ranks_t immolate;
    spell_ranks_t life_tap;
    spell_ranks_t rain_of_fire;
    spell_ranks_t searing_pain;
    spell_ranks_t shadow_bolt;
  } rank;

private:
  target_specific_t<warlock_td_t> _target_data;
};

warlock_td_t::warlock_td_t( player_t* target, warlock_t& p ) : actor_target_data_t( target, &p )
{
  dots.bane_of_agony = target->get_dot( "bane_of_agony", &p );
  dots.corruption    = target->get_dot( "corruption", &p );
  dots.immolate   = target->get_dot( "immolate", &p );
}

warlock_td_t* warlock_t::get_target_data( player_t* target ) const
{
  warlock_td_t*& td = _target_data[ target ];
  if ( !td )
  {
    td = new warlock_td_t( target, const_cast<warlock_t&>( *this ) );
  }
  return td;
}

struct warlock_module_t : public module_t
{
  warlock_module_t() : module_t( WARLOCK )
  { }

  player_t* create_player( sim_t* sim, util::string_view name, race_e r = RACE_NONE ) const override
  {
    auto p = new warlock_t( sim, name, r );
    return p;
  }

  bool valid() const override
  { return true; }

  void register_hotfixes() const override {}

  void register_actor_initializers( sim_t* ) const override {}
};

template <class Base>
struct warlock_action_t : public parse_action_effects_t<Base>
{
private:
  using ab = parse_action_effects_t<Base>;

public:
  using base_t = warlock_action_t<Base>;

  template <typename... Args>
  warlock_action_t( Args&&... args ) : ab( std::forward<Args>( args )... )
  {
    // Recasting a dot restarts it
    this->dot_behavior = DOT_CLIP;
  }

  warlock_t* p()
  { return debug_cast<warlock_t*>( this->player ); }

  const warlock_t* p() const
  { return debug_cast<const warlock_t*>( this->player ); }
};

using warlock_spell_t  = warlock_action_t<spell_t>;

// Shadow Bolt
struct shadow_bolt_t : public warlock_spell_t
{
  shadow_bolt_t( warlock_t* player, util::string_view options_str ) :
    warlock_spell_t( "shadow_bolt", player, player->rank.shadow_bolt.find_rank()->spell() )
  {
    parse_options( options_str );
  }
};

// Rain of Fire
struct rain_of_fire_tick_t : public warlock_spell_t
{
  rain_of_fire_tick_t( warlock_t* player ) :
    warlock_spell_t( "rain_of_fire_tick", player, player->rank.rain_of_fire.find_rank()->attack() )
  {
    background = dual = true;
    aoe = -1;
  }
};

struct rain_of_fire_t : public warlock_spell_t
{
  rain_of_fire_t( warlock_t* player, util::string_view options_str ) :
    warlock_spell_t( "rain_of_fire", player, player->rank.rain_of_fire.find_rank()->spell() )
  {
    parse_options( options_str );
    channeled = true;
    may_miss = may_crit = false;
    tick_action = new rain_of_fire_tick_t( player );
  }
};

// Searing Pain
struct searing_pain_t : public warlock_spell_t
{
  searing_pain_t( warlock_t* player, util::string_view options_str ) :
    warlock_spell_t( "searing_pain", player, player->rank.searing_pain.find_rank()->spell() )
  {
    parse_options( options_str );
  }
};

// Immolate
struct immolate_t : public warlock_spell_t
{
  immolate_t( warlock_t* player, util::string_view options_str ) :
    warlock_spell_t( "immolate", player, player->rank.immolate.find_rank()->spell() )
  {
    parse_options( options_str );
  }
};

// Corruption
struct corruption_t : public warlock_spell_t
{
  corruption_t( warlock_t* player, util::string_view options_str ) :
    warlock_spell_t( "corruption", player, player->rank.corruption.find_rank()->spell() )
  {
    parse_options( options_str );
  }
};

// Bane of Agony
// TODO: Only one Bane per warlock can be active on a target, implement with Bane of Doom
struct bane_of_agony_t : public warlock_spell_t
{
  bane_of_agony_t( warlock_t* player, util::string_view options_str ) :
    warlock_spell_t( "bane_of_agony", player, player->rank.bane_of_agony.find_rank()->spell() )
  {
    parse_options( options_str );
  }

  // Base damage ramps by thirds of the duration, spell power does not
  double base_ta( const action_state_t* s ) const override
  {
    double b = warlock_spell_t::base_ta( s );

    const dot_t* d = find_dot( s->target );
    if ( !d || !d->is_ticking() )
      return b;

    timespan_t elapsed = d->duration() - d->remains();

    if ( 3 * elapsed <= d->duration() )
      return b * 0.5;
    if ( 3 * elapsed <= 2 * d->duration() )
      return b;
    return b * 1.5;
  }
};

// Curse of the Elements
// TODO: Only one Curse per warlock can be active on a target, implement with the next Curse
// TODO: Resistances appear not yet implemented, implement or confirm
// TODO: Confirm in-game and in sim pierce/lower/reduce functionality
struct curse_of_the_elements_t : public warlock_spell_t
{
  curse_of_the_elements_t( warlock_t* player, util::string_view options_str ) :
    warlock_spell_t( "curse_of_the_elements", player, player->rank.curse_of_the_elements.find_rank()->spell() )
  {
    parse_options( options_str );
    background = sim->overrides.curse_of_the_elements != 0;
  }

  void impact( action_state_t* s ) override
  {
    warlock_spell_t::impact( s );

    if ( result_is_hit( s->result ) )
      s->target->debuffs.curse_of_the_elements->trigger( 1, data().effectN( 2 ).percent(), -1, data().duration() );
  }
};

// Drain Life
// TODO: Heal the caster for the damage dealt
struct drain_life_t : public warlock_spell_t
{
  drain_life_t( warlock_t* player, util::string_view options_str ) :
    warlock_spell_t( "drain_life", player, player->rank.drain_life.find_rank()->spell() )
  {
    parse_options( options_str );
    channeled = true;
  }
};

// Life Tap
struct life_tap_t : public warlock_spell_t
{
  gain_t* gain;

  life_tap_t( warlock_t* player, util::string_view options_str ) :
    warlock_spell_t( "life_tap", player, player->rank.life_tap.find_rank()->spell() ),
    gain( player->get_gain( "life_tap" ) )
  {
    parse_options( options_str );
    harmful = false;
    target = player;
  }

  void execute() override
  {
    warlock_spell_t::execute();

    double amount = data().effectN( 1 ).average( player ) + player->cache.spirit();

    player->resource_loss( RESOURCE_HEALTH, amount, nullptr, this );
    player->resource_gain( RESOURCE_MANA, amount, gain, this );
  }
};

struct summon_imp_t final : public warlock_spell_t
{
  summon_imp_t( warlock_t* player, std::string_view options_str ) :
    warlock_spell_t( "summon_imp", player, player->find_spell( 688 ) )
  {
    parse_options( options_str );
    harmful = false;
  }

  void execute() override
  {
    warlock_spell_t::execute();
    p()->pets.imp->summon();
  }
};

///////////////////////////////////////////
//
// Demons
//
///////////////////////////////////////////

namespace pets {
  struct warlock_pet_t : public pet_t
  {
    warlock_pet_t( sim_t* sim, warlock_t* owner, std::string_view pet_name, pet_e pet_type, bool guardian = false ) :
      pet_t( sim, owner, pet_name, pet_type, guardian, false )
    {
    }

    const warlock_t* o() const
    { return static_cast<warlock_t*>( owner ); }

    warlock_t* o()
    { return static_cast<warlock_t*>( owner ); }
  };

  template <class Base>
  struct warlock_pet_action_t : parse_action_effects_t<Base>
  {
  private:
    using ab = parse_action_effects_t<Base>;
  public:
    using base_t = warlock_pet_action_t<Base>;

    template <typename... Args>
    warlock_pet_action_t( Args&&... args ) : ab( std::forward<Args>( args )... ) {}

    warlock_pet_t* p()
    { return debug_cast<warlock_pet_t*>( this->player ); }

    const warlock_pet_t* p() const
    { return debug_cast<const warlock_pet_t*>( this->player ); }

    warlock_t* o()
    { return debug_cast<warlock_t*>( p()->owner ); }

    const warlock_t* o() const
    { return debug_cast<const warlock_t*>( p()->owner ); }
  };

  using warlock_pet_attack_t = warlock_pet_action_t<melee_attack_t>;
  using warlock_pet_spell_t  = warlock_pet_action_t<spell_t>;

  namespace imp {
    struct imp_pet_t : warlock_pet_t
    {
      imp_pet_t( sim_t* sim, warlock_t* owner ) :
        warlock_pet_t( sim, owner, "imp", PET_IMP )
      {
        // from level 6-10 imp character sheet
        owner_coeff.armor = 0.35;
        owner_coeff.ap_from_sp = 0.17;

        // TODO: Imp HP looks to come from a table
        owner_coeff.health = 1.5;
        stamina_per_owner = 0;

        // matches in-game stats but int might not actually do anything on the pet
        intellect_per_owner = 0.3;
      }

      resource_e primary_resource() const override { return RESOURCE_MANA; }

      // copies owner's crit which matches imp char sheet
      double pet_crit() const override
      { return current_pet_stats.composite_spell_crit; }

      double resource_regen_per_second( resource_e r ) const override
      {
        if ( r != RESOURCE_MANA )
        {
          return pet_t::resource_regen_per_second( r );
        }

        // TODO - this is at level 7, probably need a table for this
        return recent_cast() ? 1.65 : 2.73;
      }

      void init_base_stats() override
      {
        warlock_pet_t::init_base_stats();

        // TODO: figure this out - probably another table of int/mana at every level
        // fit to obsevations level 3-8 but starts to break down at level 9
        double mana = 29 + (9.5 * level());
        resources.base[ RESOURCE_MANA ] = std::floor(mana);
      }

      void init_action_list() override
      {
        action_list_str = "firebolt";
        warlock_pet_t::init_action_list();
      }

      action_t* create_action( std::string_view, std::string_view ) override;
    };

    // TODO: There's some funky delay on beta. Some casts are back-to-back, some have a 0.5-1.0s gap even when
    // the imp has mana
    struct firebolt_t : warlock_pet_spell_t
    {
      firebolt_t( std::string_view n, imp_pet_t* p, std::string_view options_str  ) :
        warlock_pet_spell_t( n, p, p->o()->rank.firebolt.find_rank()->spell() )
      {
        parse_options( options_str );

        // beta casts have been wonky with gaps in between casts
        //ability_lag.mean   = 500_ms;
        //ability_lag.stddev = 100_ms;
      }
    };

    action_t* imp_pet_t::create_action( std::string_view name, std::string_view options_str )
    {
      if ( name == "firebolt" ) return new firebolt_t( name, this, options_str );

      return warlock_pet_t::create_action( name, options_str );
    }
  }
}

action_t* warlock_t::create_action( util::string_view name, util::string_view options_str )
{
  if ( name == "shadow_bolt" )
    return new shadow_bolt_t( this, options_str );
  if ( name == "rain_of_fire" )
    return new rain_of_fire_t( this, options_str );
  if ( name == "searing_pain" )
    return new searing_pain_t( this, options_str );
  if ( name == "immolate" )
    return new immolate_t( this, options_str );
  if ( name == "corruption" )
    return new corruption_t( this, options_str );
  if ( name == "bane_of_agony" )
    return new bane_of_agony_t( this, options_str );
  if ( name == "curse_of_the_elements" )
    return new curse_of_the_elements_t( this, options_str );
  if ( name == "drain_life" )
    return new drain_life_t( this, options_str );
  if ( name == "life_tap" )
    return new life_tap_t( this, options_str );

  if ( name == "summon_imp" )
    return new summon_imp_t( this, options_str );

  return parse_player_effects_t::create_action( name, options_str );
}

void warlock_t::create_pets()
{
  parse_player_effects_t::create_pets();

  pets.imp = new pets::imp::imp_pet_t( sim, this );
}

void warlock_t::init_base_stats()
{
  parse_player_effects_t::init_base_stats();

  base.attack_crit_chance = get_passive_player_value(0.02, "all_crit");
  base.spell_crit_chance = get_passive_player_value(0.01701, "all_crit");
}

void warlock_t::init_spells()
{
  parse_player_effects_t::init_spells();

  rank.bane_of_agony = { this,
    spell_container_t().set_spell(   980 ),
    spell_container_t().set_spell(  1014 ),
    spell_container_t().set_spell(  6217 ),
    spell_container_t().set_spell( 11711 ),
    spell_container_t().set_spell( 11712 ),
    spell_container_t().set_spell( 11713 ),
  };

  rank.corruption = { this,
    spell_container_t().set_spell(   172 ),
    spell_container_t().set_spell(  6222 ),
    spell_container_t().set_spell(  6223 ),
    spell_container_t().set_spell(  7648 ),
    spell_container_t().set_spell( 11671 ),
    spell_container_t().set_spell( 11672 ),
    spell_container_t().set_spell( 25311 ),
  };

  rank.curse_of_the_elements = { this,
    spell_container_t().set_spell(  440892 ),
    spell_container_t().set_spell( 1311676 ),
    spell_container_t().set_spell( 1311677 ),
    spell_container_t().set_spell( 1311680 ),
  };

  rank.drain_life = { this,
    spell_container_t().set_spell(   689 ),
    spell_container_t().set_spell(   699 ),
    spell_container_t().set_spell(   709 ),
    spell_container_t().set_spell(  7651 ),
    spell_container_t().set_spell( 11699 ),
    spell_container_t().set_spell( 11700 ),
  };

  rank.firebolt = { this,
    spell_container_t().set_spell(  3110 ),  // 1
    spell_container_t().set_spell(  7799 ),  // 8
    spell_container_t().set_spell(  7800 ),  // 18
    spell_container_t().set_spell(  7801 ),  // 28
    spell_container_t().set_spell(  7802 ),  // 38
    spell_container_t().set_spell( 11762 ),  // 48
    spell_container_t().set_spell( 11763 ),  // 58
  };

  rank.immolate = { this,
    spell_container_t().set_spell(   348 ),
    spell_container_t().set_spell(   707 ),
    spell_container_t().set_spell(  1094 ),
    spell_container_t().set_spell(  2941 ),
    spell_container_t().set_spell( 11665 ),
    spell_container_t().set_spell( 11667 ),
    spell_container_t().set_spell( 11668 ),
    spell_container_t().set_spell( 25309 ),
  };

  rank.life_tap = { this,
    spell_container_t().set_spell(  1454 ),
    spell_container_t().set_spell(  1455 ),
    spell_container_t().set_spell(  1456 ),
    spell_container_t().set_spell( 11687 ),
    spell_container_t().set_spell( 11688 ),
    spell_container_t().set_spell( 11689 ),
  };

  rank.rain_of_fire = { this,
    spell_container_t().set_spell(  5740 ).set_attack( 1282380 ),
    spell_container_t().set_spell(  6219 ).set_attack( 1282383 ),
    spell_container_t().set_spell( 11677 ).set_attack( 1282384 ),
    spell_container_t().set_spell( 11678 ).set_attack( 1282385 ),
  };

  rank.searing_pain = { this,
    spell_container_t().set_spell(  5676 ),
    spell_container_t().set_spell( 17919 ),
    spell_container_t().set_spell( 17920 ),
    spell_container_t().set_spell( 17921 ),
    spell_container_t().set_spell( 17922 ),
    spell_container_t().set_spell( 17923 ),
  };

  rank.shadow_bolt = { this,
    spell_container_t().set_spell(   686 ),
    spell_container_t().set_spell(   695 ),
    spell_container_t().set_spell(   705 ),
    spell_container_t().set_spell(  1088 ),
    spell_container_t().set_spell(  1106 ),
    spell_container_t().set_spell(  7641 ),
    spell_container_t().set_spell( 11659 ),
    spell_container_t().set_spell( 11660 ),
    spell_container_t().set_spell( 11661 ),
    spell_container_t().set_spell( 25307 ),
  };

  parse_all_class_passives();
  parse_all_passive_talents();
}

void warlock_t::init_action_list()
{
  if ( !action_list_str.empty() )
  {
    parse_player_effects_t::init_action_list();
    return;
  }

  clear_action_priority_lists();
  warlock_apl::warlock( this );
  use_default_action_list = true;
  parse_player_effects_t::init_action_list();
}

}  // namespace warlock

const module_t* module_t::warlock()
{
  static warlock::warlock_module_t m;
  return &m;
}
