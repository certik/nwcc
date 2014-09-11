#ifndef FEATURES_H
#define FEATURES_H

/*
 * This file is for optional/temporary features. Note that good and
 * non-debug code should NOT be controlled by these macros!
 *
 * Instead they are intended to make substantial and dangerous
 * changes optional until they are tested and can be relied on
 */


#define FEAT_DEBUG_DUMP_BOGUS_STORES	0
#define FEAT_AVOID_REG_STORES		0

/*
 * Enables immediate translation of functions and most global
 * variables. That way less data structures must be kept around
 * (assuming the zone allocator is also enabled)
 *
 * Saves VERY much memory. This should always be enabled
 */
#define XLATE_IMMEDIATELY		1

/*
 * Enables zone allocator for all data structures pertaining to
 * the translation of functions. This saves a lot of memory
 *
 * Should always be enabled
 *
 * 05/22/09: Not for the preprocessor! Maybe it should be enabled
 * but the infrastructure is not there yet so we turn it off for
 * now
 */
#ifdef PREPROCESSOR
#    define USE_ZONE_ALLOCATOR		0	
#else
#    define USE_ZONE_ALLOCATOR		(1 && XLATE_IMMEDIATELY)
#endif

/*
 * Don't use ``struct arrarg'' in type_node anymore to record
 * array sizes. Now we always set arrarg_const immediately. That
 * way the expr data structures can be freed
 *
 * Should always be enabled 
 */
#define REMOVE_ARRARG			1

/*
 * Does not use the stupid static float conversion buffer on x86
 * and AMD64 anymore
 *
 * Should always be enabled
 */
#define REMOVE_FLOATBUF			1


/*
 * Evaluate constant arithmetic sub-expressions if possible
 *
 * Should always be enabled
 *
 * 05/22/09: Don't enable it for the preprocessor because the
 * evaluation functions drag in icode functions, and it's
 * probably completely unneeded in the preprocessor (we can
 * only handle fully constant expressions, not partially
 * constant ones)
 */
#ifdef PREPROCESSOR
#    define EVAL_CONST_EXPR_CT		0
#else
#    define EVAL_CONST_EXPR_CT		1
#endif

/*
 * Removes many unnecessary register saves
 *
 * Should always be enabled, but will cause a lot of headache until
 * debugged
 */
#define AVOID_REGISTER_LEAKS		0

/*
 * Lookup of identifiers in the same pass as they're encountered. This
 * fixes variable visibilty rules
 *
 * Should always be enabled
 */
#define IMMEDIATE_SYMBOL_LOOKUP		1

/*
 * Removes the handling of multi-GPR register saving in free_preg().
 * This may cause problems
 */
#define AVOID_DUPED_MULTI_REG_SAVES	1

/*
 * This may be a huge can of worms... Don't set the 4 bytes max
 * alignment limit on x86, but do it like gcc, i.e. give 8 bytes
 * alignment to long long and double. Note that these values are
 * configurable with -falign-double or somesuch!
 */
#define ALIGN_X86_LIKE_GCC		1


/*
 * 07/16/08: Since we deal with constant sub-expression evaluation
 * more ``aggressively'' now, we may run into char and short constants,
 * e.g. in
 *
 *     char func() { return (char)12; }
 *
 * ... here we could promote the operand to int, but that doesn't
 * really make sense. So the int constant is converted to a char, and
 * ends up being a constant token with type TY_CHAR
 *
 * The flag below teaches the emitters how to load such unexpected
 * constants
 */
#define ALLOW_CHAR_SHORT_CONSTANTS	1

/*
 * 05/13/09: First and foremost, this stuff is NOT fast! Maybe because
 * we are not using it for struct declarations as well. But it is 
 * certainly not an improvement.
 *
 * Traditionally we only use hash tables for the global scope and scan
 * lineary for nested scopes. On average this is no worse than the new
 * lookup. This needs to be tested more, but it was probably a foolish
 * micro optimization because of a few stupid profiler samples.
 *
 * It is DISABLED now because it STILL has bugs! Doesn't compile ftp.c
 * of wget because of a huge bogus index (stack overflow?) in symlist.c
 * on line 444
 */
#define FAST_SYMBOL_LOOKUP		0

/*
 * 08/13/09
 */
#define ZALLOC_USE_FREELIST		0

#endif

