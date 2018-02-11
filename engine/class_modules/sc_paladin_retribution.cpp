#include "sc_paladin.hpp"

namespace paladin {

namespace buffs {
  crusade_buff_t::crusade_buff_t( player_t* p ) :
      haste_buff_t( haste_buff_creator_t( p, "crusade", p -> find_spell( 231895 ) )
        .refresh_behavior( BUFF_REFRESH_DISABLED ) ),
      damage_modifier( 0.0 ),
      healing_modifier( 0.0 ),
      haste_bonus( 0.0 )
  {
    // TODO(mserrano): fix this when Blizzard turns the spelldata back to sane
    //  values
    damage_modifier = data().effectN( 1 ).percent() / 10.0;
    haste_bonus = data().effectN( 3 ).percent() / 10.0;
    healing_modifier = 0;

    paladin_t* paladin = static_cast<paladin_t*>( player );
    if ( paladin -> artifact.wrath_of_the_ashbringer.rank() )
    {
      buff_duration += timespan_t::from_millis(paladin -> artifact.wrath_of_the_ashbringer.value());
    }

    // let the ability handle the cooldown
    cooldown -> duration = timespan_t::zero();

    // invalidate Damage and Healing for both specs
    add_invalidate( CACHE_PLAYER_DAMAGE_MULTIPLIER );
    add_invalidate( CACHE_HASTE );
  }

  void crusade_buff_t::expire_override( int expiration_stacks, timespan_t remaining_duration )
  {
    buff_t::expire_override( expiration_stacks, remaining_duration );

    paladin_t* p = static_cast<paladin_t*>( player );
    p -> buffs.liadrins_fury_unleashed -> expire(); // Force Liadrin's Fury to fade
  }

  struct shield_of_vengeance_buff_t : public absorb_buff_t
  {
    shield_of_vengeance_buff_t( player_t* p ):
      absorb_buff_t( absorb_buff_creator_t( p, "shield_of_vengeance", p -> find_spell( 184662 ) ) )
    {
      paladin_t* pal = static_cast<paladin_t*>( p );
      if ( pal -> artifact.deflection.rank() )
      {
        cooldown -> duration += timespan_t::from_millis( pal -> artifact.deflection.value() );
      }
    }

    void expire_override( int expiration_stacks, timespan_t remaining_duration ) override
    {
      absorb_buff_t::expire_override( expiration_stacks, remaining_duration );

      paladin_t* p = static_cast<paladin_t*>( player );
      // do thing
      if ( p -> fake_sov )
      {
        // TODO(mserrano): This is a horrible hack
        p -> active_shield_of_vengeance_proc -> base_dd_max = p -> active_shield_of_vengeance_proc -> base_dd_min = current_value;
        p -> active_shield_of_vengeance_proc -> execute();
      }
    }
  };
}

holy_power_consumer_t::holy_power_consumer_t( const std::string& n, paladin_t* p,
                                              const spell_data_t* s, bool u2h) : paladin_melee_attack_t( n, p, s, u2h )
{
  if ( p -> sets -> has_set_bonus( PALADIN_RETRIBUTION, T19, B2 ) )
  {
    base_multiplier *= 1.0 + p -> sets -> set( PALADIN_RETRIBUTION, T19, B2 ) -> effectN( 1 ).percent();
  }
}

double holy_power_consumer_t::composite_target_multiplier( player_t* t ) const
{
  double m = paladin_melee_attack_t::composite_target_multiplier( t );

  paladin_td_t* td = this -> td( t );

  if ( td -> buffs.debuffs_judgment -> up() )
  {
    double judgment_multiplier = 1.0 + td -> buffs.debuffs_judgment -> data().effectN( 1 ).percent() + p() -> get_divine_judgment();
    judgment_multiplier += p() -> passives.judgment -> effectN( 1 ).percent();
    m *= judgment_multiplier;
  }

  return m;
}

void holy_power_consumer_t::execute()
{
  double c = cost();
  paladin_melee_attack_t::execute();
  if ( c <= 0.0 )
  {
    if ( p() -> buffs.divine_purpose -> check() )
    {
      p() -> buffs.divine_purpose -> expire();
    }
  }

  if ( p() -> talents.divine_purpose -> ok() )
  {
    bool success = p() -> buffs.divine_purpose -> trigger( 1,
      p() -> buffs.divine_purpose -> default_value,
      p() -> spells.divine_purpose_ret -> proc_chance() );
    if ( success )
      p() -> procs.divine_purpose -> occur();
  }

  if ( p() -> buffs.crusade -> check() )
  {
    int num_stacks = (int)base_cost();
    p() -> buffs.crusade -> trigger( num_stacks );
  }

  if ( p() -> artifact.righteous_verdict.rank() )
  {
    p() -> buffs.righteous_verdict -> trigger();
  }
}

// Custom events
struct whisper_of_the_nathrezim_event_t : public event_t
{
  paladin_t* paladin;

  whisper_of_the_nathrezim_event_t( paladin_t* p, timespan_t delay ) :
    event_t( *p, delay ), paladin( p )
  {
  }

  const char* name() const override
  { return "whisper_of_the_nathrezim_delay"; }

  void execute() override
  {
    paladin -> buffs.whisper_of_the_nathrezim -> trigger();
  }
};

struct scarlet_inquisitors_expurgation_expiry_event_t : public event_t
{
  paladin_t* paladin;

  scarlet_inquisitors_expurgation_expiry_event_t( paladin_t* p, timespan_t delay ) :
    event_t( *p, delay ), paladin( p )
  {
  }

  const char* name() const override
  { return "scarlet_inquisitors_expurgation_expiry_delay"; }

  void execute() override
  {
    paladin -> buffs.scarlet_inquisitors_expurgation -> expire();
  }
};

struct echoed_spell_event_t : public event_t
{
  paladin_melee_attack_t* echo;
  paladin_t* paladin;

  echoed_spell_event_t( paladin_t* p, paladin_melee_attack_t* spell, timespan_t delay ) :
    event_t( *p, delay ), echo( spell ), paladin( p )
  {
  }

  const char* name() const override
  { return "echoed_spell_delay"; }

  void execute() override
  {
    echo -> schedule_execute();
  }
};

// Crusade
struct crusade_t : public paladin_heal_t
{
  crusade_t( paladin_t* p, const std::string& options_str )
    : paladin_heal_t( "crusade", p, p -> talents.crusade )
  {
    parse_options( options_str );

    if ( ! ( p -> talents.crusade_talent -> ok() ) )
      background = true;

    cooldown -> charges += p -> sets -> set( PALADIN_RETRIBUTION, T18, B2 ) -> effectN( 1 ).base_value();
  }

  void tick( dot_t* d ) override
  {
    // override for this just in case Avenging Wrath were to get canceled or removed
    // early, or if there's a duration mismatch (unlikely, but...)
    if ( p() -> buffs.crusade -> check() )
    {
      // call tick()
      heal_t::tick( d );
    }
  }

  void execute() override
  {
    paladin_heal_t::execute();

    p() -> buffs.crusade -> trigger();

    if ( p() -> liadrins_fury_unleashed )
    {
      p() -> buffs.liadrins_fury_unleashed -> trigger();
    }
  }

  bool ready() override
  {
    if ( p() -> buffs.crusade -> check() )
      return false;
    else
      return paladin_heal_t::ready();
  }
};

// Execution Sentence =======================================================

struct execution_sentence_t : public paladin_spell_t
{
  execution_sentence_t( paladin_t* p, const std::string& options_str )
    : paladin_spell_t( "execution_sentence", p, p -> find_talent_spell( "Execution Sentence" ) )
  {
    parse_options( options_str );
    hasted_ticks   = true;
    travel_speed   = 0;
    tick_may_crit  = true;

    // disable if not talented
    if ( ! ( p -> talents.execution_sentence -> ok() ) )
      background = true;

    if ( p -> sets -> has_set_bonus( PALADIN_RETRIBUTION, T19, B2 ) )
    {
      base_multiplier *= 1.0 + p -> sets -> set( PALADIN_RETRIBUTION, T19, B2 ) -> effectN( 2 ).percent();
    }
  }

  void init() override
  {
    paladin_spell_t::init();

    update_flags &= ~STATE_HASTE;
  }

  timespan_t composite_dot_duration( const action_state_t* s ) const override
  {
    return dot_duration * ( tick_time( s ) / base_tick_time );
  }

  virtual double cost() const override
  {
    double base_cost = paladin_spell_t::cost();
    int discounts = 0;
    if ( p() -> buffs.divine_purpose -> up() )
      discounts = base_cost;
    if ( p() -> buffs.the_fires_of_justice -> up() && base_cost > discounts )
      discounts++;
    if ( p() -> buffs.ret_t21_4p -> up() && base_cost > discounts )
      discounts++;
    return base_cost - discounts;
  }

  void execute() override
  {
    double c = cost();
    paladin_spell_t::execute();

    if ( c <= 0.0 )
    {
      if ( p() -> buffs.divine_purpose -> check() )
      {
        p() -> buffs.divine_purpose -> expire();
      }
    }
    else {
      if ( p() -> buffs.the_fires_of_justice -> up() )
      {
        p() -> buffs.the_fires_of_justice -> expire();
      }
      if ( p() -> buffs.ret_t21_4p -> up() )
      {
        p() -> buffs.ret_t21_4p -> expire();
      }
    }

    if ( p() -> buffs.crusade -> check() )
    {
      int num_stacks = (int)base_cost();
      p() -> buffs.crusade -> trigger( num_stacks );
    }

    if ( p() -> talents.divine_purpose -> ok() )
    {
      bool success = p() -> buffs.divine_purpose -> trigger( 1,
        p() -> buffs.divine_purpose -> default_value,
        p() -> spells.divine_purpose_ret -> proc_chance() );
      if ( success )
        p() -> procs.divine_purpose -> occur();
    }
  }

  double composite_target_multiplier( player_t* t ) const override
  {
    double m = paladin_spell_t::composite_target_multiplier( t );

    paladin_td_t* td = this -> td( t );

    if ( td -> buffs.debuffs_judgment -> up() )
    {
      double judgment_multiplier = 1.0 + td -> buffs.debuffs_judgment -> data().effectN( 1 ).percent() + p() -> get_divine_judgment();
      judgment_multiplier += p() -> passives.judgment -> effectN( 1 ).percent();
      m *= judgment_multiplier;
    }

    return m;
  }
};


// Zeal ==========================================================

struct zeal_t : public holy_power_generator_t
{
  zeal_t( paladin_t* p, const std::string& options_str )
    : holy_power_generator_t( "zeal", p, p -> find_talent_spell( "Zeal" ), true )
  {
    parse_options( options_str );

    base_multiplier *= 1.0 + p -> artifact.blade_of_light.percent();
    base_crit += p -> artifact.sharpened_edge.percent();
    chain_multiplier = data().effectN( 1 ).chain_multiplier();

    // TODO: figure out wtf happened to this spell data
    hasted_cd = hasted_gcd = true;
  }

  int n_targets() const override
  {
    if ( p() -> buffs.zeal -> stack() )
      return 1 + p() -> buffs.zeal -> stack();
    return holy_power_generator_t::n_targets();
  }

  void execute() override
  {
    holy_power_generator_t::execute();

    p() -> buffs.zeal -> trigger();
  }
};

// Blade of Justice =========================================================

struct blade_of_justice_t : public holy_power_generator_t
{
  blade_of_justice_t( paladin_t* p, const std::string& options_str )
    : holy_power_generator_t( "blade_of_justice", p, p -> find_class_spell( "Blade of Justice" ), true )
  {
    parse_options( options_str );

    // Guarded by the Light and Sword of Light reduce base mana cost; spec-limited so only one will ever be active
    base_costs[ RESOURCE_MANA ] *= 1.0 +  p -> passives.guarded_by_the_light -> effectN( 5 ).percent();
    base_costs[ RESOURCE_MANA ] = floor( base_costs[ RESOURCE_MANA ] + 0.5 );

    base_multiplier *= 1.0 + p -> artifact.deliver_the_justice.percent();

    background = ( p -> talents.divine_hammer -> ok() );

    if ( p -> talents.virtues_blade -> ok() )
      crit_bonus_multiplier += p -> talents.virtues_blade -> effectN( 1 ).percent();
  }

  virtual double action_multiplier() const override
  {
    double am = holy_power_generator_t::action_multiplier();
    if ( p() -> buffs.righteous_verdict -> up() )
      am *= 1.0 + p() -> artifact.righteous_verdict.rank() * 0.08; // todo: fix
    if ( p() -> sets -> has_set_bonus( PALADIN_RETRIBUTION, T20, B2 ) )
      if ( p() -> buffs.sacred_judgment -> up() )
        am *= 1.0 + p() -> buffs.sacred_judgment -> data().effectN( 1 ).percent();
    return am;
  }

  virtual void execute() override
  {
    holy_power_generator_t::execute();
    if ( p() -> buffs.righteous_verdict -> up() )
      p() -> buffs.righteous_verdict -> expire();

    if ( p() -> sets -> has_set_bonus( PALADIN_RETRIBUTION, T20, B4 ) )
      p() -> resource_gain( RESOURCE_HOLY_POWER, 1, p() -> gains.hp_t20_2p );
  }
};

// Divine Hammer =========================================================

struct divine_hammer_tick_t : public paladin_melee_attack_t
{

  divine_hammer_tick_t( paladin_t* p )
    : paladin_melee_attack_t( "divine_hammer_tick", p, p -> find_spell( 198137 ) )
  {
    aoe         = -1;
    dual        = true;
    direct_tick = true;
    background  = true;
    may_crit    = true;
    ground_aoe = true;
  }
};

struct divine_hammer_t : public paladin_spell_t
{

  divine_hammer_t( paladin_t* p, const std::string& options_str )
    : paladin_spell_t( "divine_hammer", p, p -> find_talent_spell( "Divine Hammer" ) )
  {
    parse_options( options_str );

    hasted_ticks   = true;
    may_miss       = false;
    tick_may_crit  = true;
    tick_zero      = true;
    energize_type      = ENERGIZE_ON_CAST;
    energize_resource  = RESOURCE_HOLY_POWER;
    energize_amount    = data().effectN( 2 ).base_value();

    // TODO: figure out wtf happened to this spell data
    hasted_cd = hasted_gcd = true;

    base_multiplier *= 1.0 + p -> artifact.deliver_the_justice.percent();

    tick_action = new divine_hammer_tick_t( p );
  }

  void init() override
  {
    paladin_spell_t::init();

    update_flags &= ~STATE_HASTE;
  }

  timespan_t composite_dot_duration( const action_state_t* s ) const override
  {
    return dot_duration * ( tick_time( s ) / base_tick_time );
  }

  virtual double composite_persistent_multiplier( const action_state_t* s ) const override
  {
    double am = paladin_spell_t::composite_persistent_multiplier( s );
    if ( p() -> buffs.righteous_verdict -> up() )
      am *= 1.0 + p() -> artifact.righteous_verdict.rank() * 0.08; // todo: fix
    if ( p() -> sets -> has_set_bonus( PALADIN_RETRIBUTION, T20, B2 ) )
      if ( p() -> buffs.sacred_judgment -> up() )
        am *= 1.0 + p() -> buffs.sacred_judgment -> data().effectN( 1 ).percent();
    return am;
  }

  virtual void execute() override
  {
    paladin_spell_t::execute();
    if ( p() -> buffs.righteous_verdict -> up() )
      p() -> buffs.righteous_verdict -> expire();

    if ( p() -> sets -> has_set_bonus( PALADIN_RETRIBUTION, T20, B4 ) )
      p() -> resource_gain( RESOURCE_HOLY_POWER, 1, p() -> gains.hp_t20_2p );
  }
};

// Divine Storm =============================================================

struct echoed_divine_storm_t: public paladin_melee_attack_t
{
  echoed_divine_storm_t( paladin_t* p, const std::string& options_str )
    : paladin_melee_attack_t( "echoed_divine_storm", p, p -> find_spell( 224239 ), true )
  {
    parse_options( options_str );

    weapon = &( p -> main_hand_weapon );

    base_multiplier *= p -> artifact.echo_of_the_highlord.percent();

    base_multiplier *= 1.0 + p -> artifact.righteous_blade.percent();
    base_multiplier *= 1.0 + p -> artifact.divine_tempest.percent( 2 );
    if ( p -> talents.final_verdict -> ok() )
      base_multiplier *= 1.0 + p -> talents.final_verdict -> effectN( 2 ).percent();

    ret_damage_increase = true;

    aoe = -1;
    background = true;
  }

  virtual double cost() const override
  {
    return 0;
  }

  double composite_target_multiplier( player_t* t ) const override
  {
    double m = paladin_melee_attack_t::composite_target_multiplier( t );

    paladin_td_t* td = this -> td( t );

    if ( td -> buffs.debuffs_judgment -> up() )
    {
      double judgment_multiplier = 1.0 + td -> buffs.debuffs_judgment -> data().effectN( 1 ).percent() + p() -> get_divine_judgment();
      judgment_multiplier += p() -> passives.judgment -> effectN( 1 ).percent();
      m *= judgment_multiplier;
    }

    return m;
  }


  virtual double action_multiplier() const override
  {
    double am = paladin_melee_attack_t::action_multiplier();
    if ( p() -> buffs.whisper_of_the_nathrezim -> check() )
      am *= 1.0 + p() -> buffs.whisper_of_the_nathrezim -> data().effectN( 1 ).percent();
    if ( p() -> buffs.scarlet_inquisitors_expurgation -> up() )
      am *= 1.0 + p() -> buffs.scarlet_inquisitors_expurgation -> check_stack_value();
    return am;
  }
};

struct divine_storm_t: public holy_power_consumer_t
{
  echoed_divine_storm_t* echoed_spell;

  struct divine_storm_damage_t : public paladin_melee_attack_t
  {
    divine_storm_damage_t( paladin_t* p )
      : paladin_melee_attack_t( "divine_storm_dmg", p, p -> find_spell( 224239 ) )
    {
      dual = background = true;
      may_miss = may_dodge = may_parry = false;
    }
  };

  divine_storm_t( paladin_t* p, const std::string& options_str )
    : holy_power_consumer_t( "divine_storm", p, p -> find_class_spell( "Divine Storm" ) ),
      echoed_spell( new echoed_divine_storm_t( p, options_str ) )
  {
    parse_options( options_str );

    hasted_gcd = true;

    may_block = false;
    impact_action = new divine_storm_damage_t( p );
    impact_action -> stats = stats;

    weapon = &( p -> main_hand_weapon );

    base_multiplier *= 1.0 + p -> artifact.righteous_blade.percent();
    base_multiplier *= 1.0 + p -> artifact.divine_tempest.percent( 2 );
    if ( p -> talents.final_verdict -> ok() )
      base_multiplier *= 1.0 + p -> talents.final_verdict -> effectN( 2 ).percent();

    aoe = -1;

    ret_damage_increase = true;

    // TODO: Okay, when did this get reset to 1?
    weapon_multiplier = 0;
  }

  virtual double cost() const override
  {
    double base_cost = holy_power_consumer_t::cost();
    int discounts = 0;
    if ( p() -> buffs.the_fires_of_justice -> up() && base_cost > discounts )
      discounts++;
    if ( p() -> buffs.ret_t21_4p -> up() && base_cost > discounts )
      discounts++;
    return base_cost - discounts;
  }

  virtual double action_multiplier() const override
  {
    double am = holy_power_consumer_t::action_multiplier();
    if ( p() -> buffs.whisper_of_the_nathrezim -> check() )
      am *= 1.0 + p() -> buffs.whisper_of_the_nathrezim -> data().effectN( 1 ).percent();
    if ( p() -> buffs.scarlet_inquisitors_expurgation -> up() )
      am *= 1.0 + p() -> buffs.scarlet_inquisitors_expurgation -> check_stack_value();
    return am;
  }

  virtual void execute() override
  {
    double c = cost();
    holy_power_consumer_t::execute();

    if ( p() -> buffs.the_fires_of_justice -> up() && c > 0 )
      p() -> buffs.the_fires_of_justice -> expire();
    if ( p() -> buffs.ret_t21_4p -> up() && c > 0 )
      p() -> buffs.ret_t21_4p -> expire();

    if ( p() -> artifact.echo_of_the_highlord.rank() )
    {
      make_event<echoed_spell_event_t>( *sim, p(), echoed_spell, timespan_t::from_millis( 600 ) );
    }

    if ( p() -> whisper_of_the_nathrezim )
    {
      if ( p() -> buffs.whisper_of_the_nathrezim -> up() )
        p() -> buffs.whisper_of_the_nathrezim -> expire();

      make_event<whisper_of_the_nathrezim_event_t>( *sim, p(), timespan_t::from_millis( 300 ) );
    }

    if ( p() -> scarlet_inquisitors_expurgation )
    {
      make_event<scarlet_inquisitors_expurgation_expiry_event_t>( *sim, p(), timespan_t::from_millis( 800 ) );
    }
  }

  void record_data( action_state_t* ) override {}
};


// Templar's Verdict ========================================================================

struct echoed_templars_verdict_t : public paladin_melee_attack_t
{
  echoed_templars_verdict_t( paladin_t* p, const std::string& options_str )
    : paladin_melee_attack_t( "echoed_verdict", p, p -> find_spell( 224266 ), true )
  {
    parse_options( options_str );

    base_multiplier *= p -> artifact.echo_of_the_highlord.percent();
    background = true;
    base_multiplier *= 1.0 + p -> artifact.might_of_the_templar.percent();
    if ( p -> talents.final_verdict -> ok() )
      base_multiplier *= 1.0 + p -> talents.final_verdict -> effectN( 1 ).percent();

    ret_damage_increase = true;
  }

  virtual double action_multiplier() const override
  {
    double am = paladin_melee_attack_t::action_multiplier();
    if ( p() -> buffs.whisper_of_the_nathrezim -> check() )
      am *= 1.0 + p() -> buffs.whisper_of_the_nathrezim -> data().effectN( 1 ).percent();
    return am;
  }

  double composite_target_multiplier( player_t* t ) const override
  {
    double m = paladin_melee_attack_t::composite_target_multiplier( t );

    paladin_td_t* td = this -> td( t );

    if ( td -> buffs.debuffs_judgment -> up() )
    {
      double judgment_multiplier = 1.0 + td -> buffs.debuffs_judgment -> data().effectN( 1 ).percent() + p() -> get_divine_judgment();
      judgment_multiplier += p() -> passives.judgment -> effectN( 1 ).percent();
      m *= judgment_multiplier;
    }

    return m;
  }

  virtual double cost() const override
  {
    return 0;
  }
};

struct templars_verdict_t : public holy_power_consumer_t
{
  echoed_templars_verdict_t* echoed_spell;

  struct templars_verdict_damage_t : public paladin_melee_attack_t
  {
    templars_verdict_damage_t( paladin_t *p )
      : paladin_melee_attack_t( "templars_verdict_dmg", p, p -> find_spell( 224266 ) )
    {
      dual = background = true;
      may_miss = may_dodge = may_parry = false;
    }
  };

  templars_verdict_t( paladin_t* p, const std::string& options_str )
    : holy_power_consumer_t( "templars_verdict", p, p -> find_specialization_spell( "Templar's Verdict" ), true ),
    echoed_spell( new echoed_templars_verdict_t( p, options_str ) )
  {
    parse_options( options_str );

    hasted_gcd = true;

    may_block = false;
    impact_action = new templars_verdict_damage_t( p );
    impact_action -> stats = stats;

    base_multiplier *= 1.0 + p -> artifact.might_of_the_templar.percent();
    if ( p -> talents.final_verdict -> ok() )
      base_multiplier *= 1.0 + p -> talents.final_verdict -> effectN( 1 ).percent();

    ret_damage_increase = true;

  // Okay, when did this get reset to 1?
    weapon_multiplier = 0;
  }

  void record_data( action_state_t* ) override {}

  virtual double cost() const override
  {
    double base_cost = holy_power_consumer_t::cost();
    int discounts = 0;
    if ( p() -> buffs.the_fires_of_justice -> up() && base_cost > discounts )
      discounts++;
    if ( p() -> buffs.ret_t21_4p -> up() && base_cost > discounts )
      discounts++;
    return base_cost - discounts;
  }

  virtual double action_multiplier() const override
  {
    double am = holy_power_consumer_t::action_multiplier();
    if ( p() -> buffs.whisper_of_the_nathrezim -> check() )
      am *= 1.0 + p() -> buffs.whisper_of_the_nathrezim -> data().effectN( 1 ).percent();
    return am;
  }

  virtual void execute() override
  {
    // store cost for potential refunding (see below)
    double c = cost();

    holy_power_consumer_t::execute();

    // TODO: do misses consume fires of justice?
    if ( p() -> buffs.the_fires_of_justice -> up() && c > 0 )
      p() -> buffs.the_fires_of_justice -> expire();
    if ( p() -> buffs.ret_t21_4p -> up() && c > 0 )
      p() -> buffs.ret_t21_4p -> expire();

    // missed/dodged/parried TVs do not consume Holy Power
    // check for a miss, and refund the appropriate amount of HP if we spent any
    if ( result_is_miss( execute_state -> result ) && c > 0 )
    {
      p() -> resource_gain( RESOURCE_HOLY_POWER, c, p() -> gains.hp_templars_verdict_refund );
    }

    if ( p() -> artifact.echo_of_the_highlord.rank() )
    {
      make_event<echoed_spell_event_t>( *sim, p(), echoed_spell, timespan_t::from_millis( 800 ) );
    }

    if ( p() -> whisper_of_the_nathrezim )
    {
      if ( p() -> buffs.whisper_of_the_nathrezim -> up() )
        p() -> buffs.whisper_of_the_nathrezim -> expire();
      make_event<whisper_of_the_nathrezim_event_t>( *sim, p(), timespan_t::from_millis( 300 ) );
    }
  }
};

// Justicar's Vengeance
struct justicars_vengeance_t : public holy_power_consumer_t
{
  justicars_vengeance_t( paladin_t* p, const std::string& options_str )
    : holy_power_consumer_t( "justicars_vengeance", p, p -> talents.justicars_vengeance, true )
  {
    parse_options( options_str );

    hasted_gcd = true;

    weapon_multiplier = 0; // why is this needed?
  }

  virtual double cost() const override
  {
    double base_cost = holy_power_consumer_t::cost();
    int discounts = 0;
    if ( p() -> buffs.the_fires_of_justice -> up() && base_cost > discounts )
      discounts++;
    if ( p() -> buffs.ret_t21_4p -> up() && base_cost > discounts )
      discounts++;
    return base_cost - discounts;
  }

  virtual void execute() override
  {
    // store cost for potential refunding (see below)
    double c = cost();

    holy_power_consumer_t::execute();

    // TODO: do misses consume fires of justice?
    if ( p() -> buffs.the_fires_of_justice -> up() && c > 0 )
      p() -> buffs.the_fires_of_justice -> expire();

    if ( p() -> buffs.ret_t21_4p -> up() && c > 0 )
      p() -> buffs.ret_t21_4p -> expire();

    // missed/dodged/parried TVs do not consume Holy Power
    // check for a miss, and refund the appropriate amount of HP if we spent any
    if ( result_is_miss( execute_state -> result ) && c > 0 )
    {
      p() -> resource_gain( RESOURCE_HOLY_POWER, c, p() -> gains.hp_templars_verdict_refund );
    }
  }
};

// SoV

struct shield_of_vengeance_t : public paladin_absorb_t
{
  shield_of_vengeance_t( paladin_t* p, const std::string& options_str ) :
    paladin_absorb_t( "shield_of_vengeance", p, p -> find_spell( 184662 ) )
  {
    parse_options( options_str );

    harmful = false;
    use_off_gcd = true;
    trigger_gcd = timespan_t::zero();


    may_crit = true;
    attack_power_mod.direct = 20;
    if ( p -> artifact.deflection.rank() )
    {
      cooldown -> duration += timespan_t::from_millis( p -> artifact.deflection.value() );
    }
  }

  void init() override
  {
    paladin_absorb_t::init();
    snapshot_flags |= (STATE_CRIT | STATE_VERSATILITY);
  }

  virtual void execute() override
  {
    paladin_absorb_t::execute();

    p() -> buffs.shield_of_vengeance -> trigger();
  }
};


// Wake of Ashes (Retribution) ================================================

struct wake_of_ashes_t : public paladin_spell_t
{
  wake_of_ashes_t( paladin_t* p, const std::string& options_str )
    : paladin_spell_t( "wake_of_ashes", p, p -> find_spell( 205273 ) )
  {
    parse_options( options_str );

    may_crit = true;
    aoe = -1;
    hasted_ticks = false;
    tick_may_crit = true;

    if ( p -> artifact.ashes_to_ashes.rank() )
    {
      energize_type = ENERGIZE_ON_HIT;
      energize_resource = RESOURCE_HOLY_POWER;
      energize_amount = p -> find_spell( 218001 ) -> effectN( 1 ).resource( RESOURCE_HOLY_POWER );
    }
    else
    {
      attack_power_mod.tick = 0;
      dot_duration = timespan_t::zero();
    }
  }

  bool ready() override
  {
    if ( ! player -> artifact->enabled() )
    {
      return false;
    }

    if ( p() -> artifact.wake_of_ashes.rank() == 0 )
    {
      return false;
    }

    return paladin_spell_t::ready();
  }
};

// Holy Wrath (Retribution) ===================================================

struct holy_wrath_t : public paladin_spell_t
{
  holy_wrath_t( paladin_t* p, const std::string& options_str )
    : paladin_spell_t( "holy_wrath", p, p -> find_talent_spell( "Holy Wrath" ) )
  {
    parse_options( options_str );

    aoe = data().effectN( 2 ).base_value();

    if ( ! ( p -> talents.holy_wrath -> ok() ) )
      background = true;
  }

  double calculate_direct_amount( action_state_t* state ) const override
  {
    double base_amount = 0;
    if ( p() -> fixed_holy_wrath_health_pct > 0 )
      base_amount = p() -> max_health() * ( 100 - p() -> fixed_holy_wrath_health_pct ) / 100.0;
    else
      base_amount = p() -> max_health() - p() -> current_health();
    double amount = base_amount * data().effectN( 3 ).percent();

    state -> result_total = amount;
    return amount;
  }

  bool ready() override
  {
    if ( p() -> fixed_holy_wrath_health_pct > 0 && p() -> fixed_holy_wrath_health_pct < 100 )
      return paladin_spell_t::ready();

    if ( player -> current_health() >= player -> max_health() )
      return false;

    return paladin_spell_t::ready();
  }
};

// Initialization
action_t* paladin_t::create_action_retribution( const std::string& name, const std::string& options_str )
{
  if ( name == "crusade"                   ) return new crusade_t                  ( this, options_str );
  if ( name == "zeal"                      ) return new zeal_t                     ( this, options_str );
  if ( name == "blade_of_justice"          ) return new blade_of_justice_t         ( this, options_str );
  if ( name == "divine_hammer"             ) return new divine_hammer_t            ( this, options_str );
  if ( name == "divine_storm"              ) return new divine_storm_t             ( this, options_str );
  if ( name == "execution_sentence"        ) return new execution_sentence_t       ( this, options_str );
  if ( name == "templars_verdict"          ) return new templars_verdict_t         ( this, options_str );
  if ( name == "holy_wrath"                ) return new holy_wrath_t               ( this, options_str );
  if ( name == "wake_of_ashes"             ) return new wake_of_ashes_t            ( this, options_str );
  if ( name == "justicars_vengeance"       ) return new justicars_vengeance_t      ( this, options_str );
  if ( name == "shield_of_vengeance"       ) return new shield_of_vengeance_t      ( this, options_str );

  return nullptr;
}

void paladin_t::create_buffs_retribution()
{
  buffs.crusade                = new buffs::crusade_buff_t( this );

  buffs.zeal                           = buff_creator_t( this, "zeal", find_spell( 217020 ) );
  buffs.the_fires_of_justice           = buff_creator_t( this, "the_fires_of_justice", find_spell( 209785 ) );
  buffs.blade_of_wrath               = buff_creator_t( this, "blade_of_wrath", find_spell( 231843 ) );
  buffs.whisper_of_the_nathrezim       = buff_creator_t( this, "whisper_of_the_nathrezim", find_spell( 207635 ) );
  buffs.liadrins_fury_unleashed        = new buffs::liadrins_fury_unleashed_t( this );
  buffs.shield_of_vengeance            = new buffs::shield_of_vengeance_buff_t( this );
  buffs.righteous_verdict              = buff_creator_t( this, "righteous_verdict", find_spell( 238996 ) );
  buffs.sacred_judgment                = buff_creator_t( this, "sacred_judgment", find_spell( 246973 ) );

  buffs.scarlet_inquisitors_expurgation = buff_creator_t( this, "scarlet_inquisitors_expurgation", find_spell( 248289 ) )
                                          .default_value( find_spell( 248289 ) -> effectN( 1 ).percent() );
  buffs.scarlet_inquisitors_expurgation_driver = buff_creator_t( this, "scarlet_inquisitors_expurgation_driver", find_spell( 248103 ) )
                                                 .period( find_spell( 248103 ) -> effectN( 1 ).period() )
                                                 .quiet( true )
                                                 .tick_callback([this](buff_t*, int, const timespan_t&) { buffs.scarlet_inquisitors_expurgation -> trigger(); })
                                                 .tick_time_behavior( BUFF_TICK_TIME_UNHASTED );

  buffs.last_defender = buff_creator_t( this, "last_defender", talents.last_defender  )
    .chance( talents.last_defender -> ok() )
    .max_stack( 99 ) //Spell doesn't cite any limits, just has diminishing returns.
    .add_invalidate( CACHE_PLAYER_DAMAGE_MULTIPLIER );

  // Tier Bonuses
  buffs.ret_t21_4p             = buff_creator_t( this, "hidden_retribution_t21_4p", find_spell( 253806 ) );
}

void paladin_t::init_rng_retribution()
{
  blade_of_wrath_rppm = get_rppm( "blade_of_wrath", find_spell( 231832 ) );
}

void paladin_t::init_spells_retribution()
{
  // talents
  talents.final_verdict              = find_talent_spell( "Final Verdict" );
  talents.execution_sentence         = find_talent_spell( "Execution Sentence" );
  talents.consecration               = find_talent_spell( "Consecration" );
  talents.fires_of_justice           = find_talent_spell( "The Fires of Justice" );
  talents.greater_judgment           = find_talent_spell( "Greater Judgment" );
  talents.zeal                       = find_talent_spell( "Zeal" );
  talents.fist_of_justice            = find_talent_spell( "Fist of Justice" );
  talents.repentance                 = find_talent_spell( "Repentance" );
  talents.blinding_light             = find_talent_spell( "Blinding Light" );
  talents.virtues_blade              = find_talent_spell( "Virtue's Blade" );
  talents.blade_of_wrath             = find_talent_spell( "Blade of Wrath" );
  talents.divine_hammer              = find_talent_spell( "Divine Hammer" );
  talents.justicars_vengeance        = find_talent_spell( "Justicar's Vengeance" );
  talents.eye_for_an_eye             = find_talent_spell( "Eye for an Eye" );
  talents.word_of_glory              = find_talent_spell( "Word of Glory" );
  talents.divine_intervention        = find_talent_spell( "Divine Intervention" );
  talents.divine_steed               = find_talent_spell( "Divine Steed" );
  talents.divine_purpose             = find_talent_spell( "Divine Purpose" ); // TODO: fix this
  talents.crusade                    = find_spell( 231895 );
  talents.crusade_talent             = find_talent_spell( "Crusade" );
  talents.holy_wrath                 = find_talent_spell( "Holy Wrath" );

  // artifact
  artifact.wake_of_ashes                = find_artifact_spell( "Wake of Ashes" );
  artifact.deliver_the_justice          = find_artifact_spell( "Deliver the Justice" );
  artifact.highlords_judgment           = find_artifact_spell( "Highlord's Judgment" );
  artifact.righteous_blade              = find_artifact_spell( "Righteous Blade" );
  artifact.divine_tempest               = find_artifact_spell( "Divine Tempest" );
  artifact.might_of_the_templar         = find_artifact_spell( "Might of the Templar" );
  artifact.sharpened_edge               = find_artifact_spell( "Sharpened Edge" );
  artifact.blade_of_light               = find_artifact_spell( "Blade of Light" );
  artifact.echo_of_the_highlord         = find_artifact_spell( "Echo of the Highlord" );
  artifact.wrath_of_the_ashbringer      = find_artifact_spell( "Wrath of the Ashbringer" );
  artifact.endless_resolve              = find_artifact_spell( "Endless Resolve" );
  artifact.deflection                   = find_artifact_spell( "Deflection" );
  artifact.ashbringers_light            = find_artifact_spell( "Ashbringer's Light" );
  artifact.ferocity_of_the_silver_hand  = find_artifact_spell( "Ferocity of the Silver Hand" );
  artifact.ashes_to_ashes               = find_artifact_spell( "Ashes to Ashes" );
  artifact.righteous_verdict            = find_artifact_spell( "Righteous Verdict" );
  artifact.judge_unworthy               = find_artifact_spell( "Judge Unworthy" );
  artifact.blessing_of_the_ashbringer   = find_artifact_spell( "Blessing of the Ashbringer" );

  // misc spells
  spells.divine_purpose_ret            = find_spell( 223817 );
  spells.liadrins_fury_unleashed       = find_spell( 208408 );
  spells.justice_gaze                  = find_spell( 211557 );
  spells.chain_of_thrayn               = find_spell( 206338 );
  spells.ashes_to_dust                 = find_spell( 236106 );
  spells.blessing_of_the_ashbringer    = find_spell( 242981 );

  // Mastery
  passives.divine_judgment             = find_mastery_spell( PALADIN_RETRIBUTION );

  // Spec aura
  spec.retribution_paladin = find_specialization_spell( "Retribution Paladin" );

  if ( specialization() == PALADIN_RETRIBUTION )
  {
    spec.judgment_2 = find_specialization_spell( 231661 );
    spec.judgment_3 = find_specialization_spell( 231663 );
  }
}

void paladin_t::init_assessors_retribution()
{
  if ( artifact.judge_unworthy.rank() )
  {
    paladin_t* p = this;
    assessor_out_damage.add(
      assessor::TARGET_DAMAGE - 1,
      [ p ]( dmg_e, action_state_t* state )
      {
        buff_t* judgment = p -> get_target_data( state -> target ) -> buffs.debuffs_judgment;
        if ( judgment -> check() )
        {
          if ( p -> rng().roll(0.5) )
            return assessor::CONTINUE;

          // Do the spread
          double max_distance  = 10.0;
          unsigned max_targets = 1;
          std::vector<player_t*> valid_targets;
          range::remove_copy_if(
              p->sim->target_list.data(), std::back_inserter( valid_targets ),
              [p, state, max_distance]( player_t* plr ) {
                paladin_td_t* td = p -> get_target_data( plr );
                if ( td -> buffs.debuffs_judgment -> check() )
                  return true;
                return state -> target -> get_player_distance( *plr ) > max_distance;
              } );
          if ( valid_targets.size() > max_targets )
          {
            valid_targets.resize( max_targets );
          }
          for ( player_t* target : valid_targets )
          {
            p -> get_target_data( target ) -> buffs.debuffs_judgment -> trigger(
                judgment -> check(),
                judgment -> check_value(),
                -1.0,
                judgment -> remains()
              );
          }
        }
        return assessor::CONTINUE;
      }
    );
  }
}

// Action Priority List Generation
void paladin_t::generate_action_prio_list_ret()
{
  action_priority_list_t* precombat = get_action_priority_list( "precombat" );

  //Flask
  if ( sim -> allow_flasks && true_level >= 80 )
  {
    std::string flask_action = "flask,type=";
    if ( true_level > 100 )
      flask_action += "flask_of_the_countless_armies";
    else if ( true_level > 90 )
      flask_action += "greater_draenic_strength_flask";
    else if ( true_level > 85 )
      flask_action += "winters_bite";
    else
      flask_action += "titanic_strength";

    precombat -> add_action( flask_action );
  }

  // Food
  if ( sim -> allow_food && level() >= 80 )
  {
    std::string food_action = "food,type=";
    if ( level() > 100 )
      food_action += "azshari_salad";
    else if ( level() > 90 )
      food_action += "sleeper_sushi";
    else
      food_action += ( level() > 85 ) ? "black_pepper_ribs_and_shrimp" : "beer_basted_crocolisk";

    precombat -> add_action( food_action );
  }

  if ( true_level > 100 )
    precombat -> add_action( "augmentation,type=defiled" );

  // Snapshot stats
  precombat -> add_action( "snapshot_stats", "Snapshot raid buffed stats before combat begins and pre-potting is done." );

  // Pre-potting
  if ( sim -> allow_potions && true_level >= 80 )
  {
    if ( true_level > 100 )
      precombat -> add_action( "potion,name=old_war" );
    else if ( true_level > 90 )
      precombat -> add_action( "potion,name=draenic_strength" );
    else
      precombat -> add_action( ( true_level > 85 ) ? "potion,name=mogu_power" : "potion,name=golemblood" );
  }

  ///////////////////////
  // Action Priority List
  ///////////////////////

  action_priority_list_t* def = get_action_priority_list( "default" );
  action_priority_list_t* opener = get_action_priority_list( "opener" );
  action_priority_list_t* cds = get_action_priority_list( "cooldowns" );
  action_priority_list_t* generators = get_action_priority_list( "generators" );
  action_priority_list_t* finishers = get_action_priority_list( "finishers" );

  def -> add_action( "auto_attack" );
  def -> add_action( this, "Rebuke" );
  def -> add_action( "call_action_list,name=opener,if=time<2" );
  def -> add_action( "call_action_list,name=cooldowns" );
  def -> add_action( "call_action_list,name=generators" );

  // Items
  int num_items = ( int ) items.size();
  for ( int i = 0; i < num_items; i++ )
  {
    if ( items[i].has_special_effect( SPECIAL_EFFECT_SOURCE_NONE, SPECIAL_EFFECT_USE ) )
    {
      std::string item_str;
      if ( items[i].name_str == "forgefiends_fabricator" )
      {
        item_str = "use_item,name=" + items[i].name_str + ",if=equipped.144358&dot.wake_of_ashes.remains<gcd*2|(buff.crusade.up&buff.crusade.remains<gcd*2|buff.avenging_wrath.up&buff.avenging_wrath.remains<gcd*2)";
        cds -> add_action( item_str );
      }
      if ( items[i].name_str == "draught_of_souls" )
      {
        item_str = "use_item,name=" + items[i].name_str + ",if=(buff.avenging_wrath.up|buff.crusade.up&buff.crusade.stack>=15|cooldown.crusade.remains>20&!buff.crusade.up)";
        cds -> add_action( item_str );
      }
      else if ( items[i].name_str == "might_of_krosus" )
      {
        item_str = "use_item,name=" + items[i].name_str + ",if=(buff.avenging_wrath.up|buff.crusade.up&buff.crusade.stack>=15|cooldown.crusade.remains>5&!buff.crusade.up)";
        cds -> add_action( item_str );
      }
      else if ( items[i].name_str == "kiljaedens_burning_wish" )
      {
        item_str = "use_item,name=" + items[i].name_str + ",if=!raid_event.adds.exists|raid_event.adds.in>35";
        cds -> add_action( item_str );
      }
      else if ( items[i].name_str == "specter_of_betrayal" )
      {
        item_str = "use_item,name=" + items[i].name_str + ",if=(buff.crusade.up&buff.crusade.stack>=15|cooldown.crusade.remains>gcd*2)|(buff.avenging_wrath.up|cooldown.avenging_wrath.remains>gcd*2)";
        cds -> add_action( item_str );
      }
      else if ( items[i].name_str == "umbral_moonglaives" )
      {
        item_str = "use_item,name=" + items[i].name_str + ",if=(buff.avenging_wrath.up|buff.crusade.up&buff.crusade.stack>=15)";
        cds -> add_action( item_str );
      }
      else if ( items[i].name_str == "vial_of_ceaseless_toxins" )
      {
        item_str = "use_item,name=" + items[i].name_str + ",if=(buff.avenging_wrath.up|buff.crusade.up&buff.crusade.stack>=15)|(cooldown.crusade.remains>30&!buff.crusade.up|cooldown.avenging_wrath.remains>30)";
        cds -> add_action( item_str );
      }
      else if ( items[i].slot != SLOT_WAIST )
      {
        item_str = "use_item,name=" + items[i].name_str + ",if=(buff.avenging_wrath.up|buff.crusade.up)";
        cds -> add_action( item_str );
      }
    }
  }

  if ( sim -> allow_potions )
  {
    if ( true_level > 100 )
      cds -> add_action( "potion,name=old_war,if=(buff.bloodlust.react|buff.avenging_wrath.up|buff.crusade.up&buff.crusade.remains<25|target.time_to_die<=40)" );
    else if ( true_level > 90 )
      cds -> add_action( "potion,name=draenic_strength,if=(buff.bloodlust.react|buff.avenging_wrath.up|target.time_to_die<=40)" );
    else if ( true_level > 85 )
      cds -> add_action( "potion,name=mogu_power,if=(buff.bloodlust.react|buff.avenging_wrath.up|target.time_to_die<=40)" );
    else if ( true_level >= 80 )
      cds -> add_action( "potion,name=golemblood,if=buff.bloodlust.react|buff.avenging_wrath.up|target.time_to_die<=40" );
  }

  std::vector<std::string> racial_actions = get_racial_actions();
  for ( size_t i = 0; i < racial_actions.size(); i++ )
  {

    if ( racial_actions[i] == "arcane_torrent" )
    {
      opener -> add_action( "arcane_torrent,if=!set_bonus.tier20_2pc" );
      cds -> add_action( "arcane_torrent,if=(buff.crusade.up|buff.avenging_wrath.up)&holy_power=2&(cooldown.blade_of_justice.remains>gcd|cooldown.divine_hammer.remains>gcd)" );
    }
    else
    {
      opener -> add_action( racial_actions[ i ] );
      cds -> add_action( racial_actions[ i ] );
    }
  }
  cds -> add_talent( this, "Holy Wrath" );
  cds -> add_action( this, "Shield of Vengeance" );
  cds -> add_action( this, "Avenging Wrath" );
  cds -> add_talent( this, "Crusade", "if=holy_power>=3|((equipped.137048|race.blood_elf)&holy_power>=2)" );

  opener -> add_action( this, "Judgment" );
  opener -> add_action( this, "Blade of Justice", "if=equipped.137048|race.blood_elf|!cooldown.wake_of_ashes.up" );
  opener -> add_talent( this, "Divine Hammer", "if=equipped.137048|race.blood_elf|!cooldown.wake_of_ashes.up" );
  opener -> add_action( this, "Wake of Ashes" );

  finishers -> add_talent( this, "Execution Sentence", "if=spell_targets.divine_storm<=3&(cooldown.judgment.remains<gcd*4.25|debuff.judgment.remains>gcd*4.25)" );
  finishers -> add_action( this, "Divine Storm", "if=debuff.judgment.up&variable.ds_castable&buff.divine_purpose.react" );
  finishers -> add_action( this, "Divine Storm", "if=debuff.judgment.up&variable.ds_castable&(!talent.crusade.enabled|cooldown.crusade.remains>gcd*2)" );
  finishers -> add_talent( this, "Justicar's Vengeance", "if=debuff.judgment.up&buff.divine_purpose.react&!equipped.137020" );
  finishers -> add_action( this, "Templar's Verdict", "if=debuff.judgment.up&buff.divine_purpose.react" );
  finishers -> add_action( this, "Templar's Verdict", "if=debuff.judgment.up&(!talent.crusade.enabled|cooldown.crusade.remains>gcd*2)&(!talent.execution_sentence.enabled|cooldown.execution_sentence.remains>gcd)" );

  generators -> add_action( "variable,name=ds_castable,value=spell_targets.divine_storm>=2|(buff.scarlet_inquisitors_expurgation.stack>=29&(equipped.144358&(dot.wake_of_ashes.ticking&time>10|dot.wake_of_ashes.remains<gcd))|(buff.scarlet_inquisitors_expurgation.stack>=29&(buff.avenging_wrath.up|buff.crusade.up&buff.crusade.stack>=15|cooldown.crusade.remains>15&!buff.crusade.up)|cooldown.avenging_wrath.remains>15)&!equipped.144358)" );
  generators -> add_action( "call_action_list,name=finishers,if=(buff.crusade.up&buff.crusade.stack<15|buff.liadrins_fury_unleashed.up)|(artifact.ashes_to_ashes.enabled&cooldown.wake_of_ashes.remains<gcd*2)" );
  generators -> add_action( "call_action_list,name=finishers,if=talent.execution_sentence.enabled&(cooldown.judgment.remains<gcd*4.25|debuff.judgment.remains>gcd*4.25)&cooldown.execution_sentence.up|buff.whisper_of_the_nathrezim.up&buff.whisper_of_the_nathrezim.remains<gcd*1.5" );
  generators -> add_action( this, "Judgment", "if=dot.execution_sentence.ticking&dot.execution_sentence.remains<gcd*2&debuff.judgment.remains<gcd*2|set_bonus.tier21_4pc" );
  generators -> add_action( this, "Blade of Justice", "if=holy_power<=2&(set_bonus.tier20_2pc|set_bonus.tier20_4pc)" );
  generators -> add_talent( this, "Divine Hammer", "if=holy_power<=2&(set_bonus.tier20_2pc|set_bonus.tier20_4pc)" );
  generators -> add_action( this, "Wake of Ashes", "if=(!raid_event.adds.exists|raid_event.adds.in>15)&(holy_power<=0|holy_power=1&(cooldown.blade_of_justice.remains>gcd|cooldown.divine_hammer.remains>gcd)|holy_power=2&((cooldown.zeal.charges_fractional<=0.65|cooldown.crusader_strike.charges_fractional<=0.65)))" );
  generators -> add_action( this, "Blade of Justice", "if=holy_power<=3&!set_bonus.tier20_4pc" );
  generators -> add_talent( this, "Divine Hammer", "if=holy_power<=3&!set_bonus.tier20_4pc" );
  generators -> add_action( this, "Judgment" );
  generators -> add_action( "call_action_list,name=finishers,if=buff.divine_purpose.up" );
  generators -> add_talent( this, "Zeal", "if=cooldown.zeal.charges_fractional>=1.65&holy_power<=4&(cooldown.blade_of_justice.remains>gcd*2|cooldown.divine_hammer.remains>gcd*2)&debuff.judgment.remains>gcd" );
  generators -> add_action( this, "Crusader Strike", "if=cooldown.crusader_strike.charges_fractional>=1.65&holy_power<=4&(cooldown.blade_of_justice.remains>gcd*2|cooldown.divine_hammer.remains>gcd*2)&debuff.judgment.remains>gcd&(talent.greater_judgment.enabled|!set_bonus.tier20_4pc&talent.the_fires_of_justice.enabled)" );
  generators -> add_talent( this, "Consecration" );
  generators -> add_action( this, "Hammer of Justice", "if=equipped.137065&target.health.pct>=75&holy_power<=4" );
  generators -> add_action( "call_action_list,name=finishers" );
  generators -> add_talent( this, "Zeal" );
  generators -> add_action( this, "Crusader Strike" );
}

} // end namespace paladin