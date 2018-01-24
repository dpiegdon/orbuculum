/*
 * Stats interface for Blackmagic Probe and TTL Serial Interfaces
 * ==============================================================
 *
 * Copyright (C) 2017  Dave Marples  <dave@marples.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This code uses the bdf library. It originally used libdwarf, but that
 * was painful. Unfortunately bdf isn't well documented so you've got to
 * go through the binutils source to find your way around it.  The bdf
 * dependencies are well partitioned into the _lookup routine and it's
 * supporting cast.
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <elf.h>
#include "bfd_wrapper.h"
#if defined OSX
    #include <libusb.h>
#else
    #if defined LINUX
        #include <libusb-1.0/libusb.h>
    #else
        #error "Unknown OS"
    #endif
#endif
#include <stdint.h>
#include <limits.h>
#include <termios.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "git_version_info.h"
#include "uthash.h"
#include "generics.h"
#include "tpiuDecoder.h"
#include "itmDecoder.h"
#include "symbols.h"

#define TEXT_SEGMENT ".text"
#define DEFAULT_TRACE_CHANNEL  30            /* Channel that we expect trace data to arrive on */
#define DEFAULT_FILE_CHANNEL   29            /* Channel that we expect file data to arrive on */

#define SERVER_PORT 3443                     /* Server port definition */
#define TRANSFER_SIZE (4096)                 /* Maximum packet we might receive */
#define TOP_UPDATE_INTERVAL (1000LL)         /* Interval between each on screen update */

/* Interface to/from target */
#define COMMS_MASK (0xF0000000)
#define IN_EVENT   (0x40000000)
#define OUT_EVENT  (0x50000000)

/* An entry in the names table */
struct nameEntryHash
{
    struct nameEntry *n;
    bool seen;
    uint32_t index;
    UT_hash_handle hh;
};

/* A calling edge */
struct edge
{
    uint32_t src;
    uint32_t dst;
    uint64_t tstamp;
    bool in;
};

/* Processed subcalls from routine to routine */
struct subcalls
{
    uint32_t src;
    uint32_t dst;
    uint64_t myCost;
    uint64_t total;
};

/* States for sample reception state machine */
enum CDState { CD_waitinout, CD_waitsrc, CD_waitdst };

/* ---------- CONFIGURATION ----------------- */
struct                                       /* Record for options, either defaults or from command line */
{
    bool useTPIU;                            /* Are we decoding via the TPIU? */
    uint32_t tpiuITMChannel;                 /* What channel? */
    bool forceITMSync;                       /* Do we assume ITM starts synced? */

    char *deleteMaterial;                    /* Material to strip off front of filenames for target */

    char *elffile;                           /* Target program config */

    char *dotfile;                           /* File to output dot information */
    char *profile;                           /* File to output profile information */

    int traceChannel;                        /* ITM Channel used for trace */
    int fileChannel;                         /* ITM Channel used for file output */
    int port;                                /* Source information for where to connect to */
    char *server;

} options =
{
    .forceITMSync = true,
    .tpiuITMChannel = 1,
    .port = SERVER_PORT,
    .server = "localhost",
    .traceChannel = DEFAULT_TRACE_CHANNEL,
    .fileChannel = DEFAULT_FILE_CHANNEL
};

/* ----------- LIVE STATE ----------------- */
struct
{
    struct ITMDecoder i;                    /* The decoders and the packets from them */
    struct ITMPacket h;
    struct TPIUDecoder t;
    struct TPIUPacket p;

    /* Calls related info */
    enum CDState CDState;                   /* State of the call data machine */
    struct edge callsConstruct;             /* Call data entry under construction */
    struct edge *calls;                     /* Call data table */
    struct subcalls *sub;                   /* Construct data */

    uint32_t subPsn;                        /* Counter for sub calls */
    uint32_t psn;                           /* Current position in assessment of data */
    uint32_t cdCount;                       /* Call data count */

    struct SymbolSet *s;                    /* Symbols read from elf */
    FILE *c;                                /* Writable file */

    /* Turn addresses into files and routines tags */
    uint32_t nameCount;
    struct nameEntryHash *name;

    /* Used for stretching number of bits in target timer */
    uint32_t oldt;
    uint64_t highOrdert;
} _r;

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================


// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Handler for individual message types from SWO
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void _handleSW( struct ITMDecoder *i )

{
    struct ITMPacket p;
    uint32_t d;

    if ( ITMGetPacket( i, &p ) )
    {
        if ( p.srcAddr == options.traceChannel )
        {
            d = ( p.d[3] << 24 ) | ( p.d[2] << 16 ) | ( p.d[1] << 8 ) | p.d[0];

            switch ( _r.CDState )
            {
                // --------------------
                case CD_waitinout:
                    if ( ( d & COMMS_MASK ) == IN_EVENT )
                    {
                        _r.callsConstruct.in = true;
                        _r.CDState = CD_waitsrc;
                    }

                    if ( ( d & COMMS_MASK ) == OUT_EVENT )
                    {
                        _r.callsConstruct.in = false;
                        _r.CDState = CD_waitsrc;
                    }

                    /* Time is encoded in lowest two octets ...accomodate rollover */
                    uint32_t t = d & 0xFFFF;

                    if ( t < _r.oldt )
                    {
                        _r.highOrdert++;
                    }

                    _r.oldt = t;
                    _r.callsConstruct.tstamp = ( _r.highOrdert << 16 ) | t;
                    break;

                // --------------------
                case CD_waitsrc:
                    /* Source address is the address of the _return_, so subtract 4 */
                    _r.callsConstruct.src = ( d - 4 );
                    _r.CDState = CD_waitdst;
                    break;

                // --------------------
                case CD_waitdst:
                    _r.callsConstruct.dst = d;
                    _r.CDState = CD_waitinout;

                    /* Now store this for later processing */
                    _r.calls = ( struct edge * )realloc( _r.calls, sizeof( struct edge ) * ( _r.cdCount + 1 ) );
                    memcpy( &_r.calls[_r.cdCount], &_r.callsConstruct, sizeof( struct edge ) );
                    _r.cdCount++;
                    break;
                    // --------------------
            }
        }
    }
}
// ====================================================================================================
void _lookup( struct nameEntryHash **h, uint32_t addr )

/* Lookup function for address to line, and hence to function, and cache in case we need it later */

{
    struct nameEntry *np;

    HASH_FIND_INT( _r.name, &addr, *h );

    if ( !( *h ) )
    {
        struct nameEntry ne;

        /* Find a matching name record if there is one */
        SymbolLookup( _r.s, addr, &ne, options.deleteMaterial );

        /* Was found, so create new hash entry for this */
        np = ( struct nameEntry * )malloc( sizeof( struct nameEntry ) );
        *h = ( struct nameEntryHash * )malloc( sizeof( struct nameEntryHash ) );
        memcpy( np, &ne, sizeof( struct nameEntry ) );
        ( *h )->n = np;
        ( *h )->index = _r.nameCount++;
        ( *h )->seen = false;

        HASH_ADD_INT( _r.name, n->addr, *h );
    }
}
// ====================================================================================================
void _flushHash( void )

{
    struct nameEntryHash *a;
    UT_hash_handle hh;

    for ( a = _r.name; a != NULL; a = hh.next )
    {
        hh = a->hh;
        free( a );
    }

    _r.name = NULL;
}
// ====================================================================================================
uint64_t _timestamp( void )

/* Return a timestamp */

{
    struct timeval te;
    gettimeofday( &te, NULL ); // get current time
    uint64_t milliseconds = te.tv_sec * 1000LL + te.tv_usec / 1000; // caculate milliseconds
    return milliseconds;
}
// ====================================================================================================
int _addresses_sort_fn( const void *a, const void *b )

/* Sort addresses first by src, then by dst */

{
    int32_t c = ( ( ( struct subcalls * )a )->src ) - ( ( ( struct subcalls * )b )->src );

    if ( c )
    {
        return c;
    }

    return ( ( ( struct subcalls * )a )->dst ) - ( ( ( struct subcalls * )b )->dst );
}
// ====================================================================================================
int _addresses_sort_dest_fn( const void *a, const void *b )

/* Sort addresses first by dst, then by src */

{
    int32_t c = ( ( ( struct subcalls * )a )->dst ) - ( ( ( struct subcalls * )b )->dst );

    if ( c )
    {
        return c;
    }

    return ( ( ( struct subcalls * )a )->src ) - ( ( ( struct subcalls * )b )->src );
}
// ====================================================================================================
void _dumpProfile( void )

/* Dump profile to Valgrind (KCacheGrind compatible) file format */

{
    struct nameEntryHash *f, *t;

    uint64_t myCost;
    uint64_t totalCost;
    uint32_t totalCalls;

    /* Empty the 'seen' field of the name cache */
    HASH_ITER( hh, _r.name, f, t )
    {
        f->seen = false;
    }

    /* Record any destination routine and the time it's taken */
    qsort( _r.sub, _r.subPsn, sizeof( struct subcalls ), _addresses_sort_dest_fn );

    for ( uint32_t i = 0; i < _r.subPsn - 1; i++ )
    {
        /* Collect total cost and sub costs for this dest routine */
        myCost = _r.sub[i].myCost;

        while ( ( i < _r.subPsn - 1 ) && ( _r.sub[i].dst == _r.sub[i + 1].dst ) )
        {
            myCost += _r.sub[i++].myCost;
        }

        _lookup( &t, _r.sub[i].dst );

        if ( !t->seen )
        {
            /* Haven't seen it before, so announce it */
            fprintf( _r.c, "fl=(%d) %s\nfn=(%d) %s\n0x%08x %d %ld\n", t->index, t->n->filename, t->index, t->n->function, t->n->addr, t->n->line, myCost );
            t->seen = true;
        }
    }


    /* OK, now proceed to report the calls */

    fprintf( _r.c, "\n\n## ------------------- Calls Follow ------------------------\n" );

    for ( uint32_t i = 0; i < _r.subPsn - 2; i++ )
    {
        myCost = _r.sub[i].myCost;
        totalCost = _r.sub[i].total;
        totalCalls = 1;

        while ( ( i < _r.subPsn - 2 ) && ( _r.sub[i].dst == _r.sub[i + 1].dst ) && ( _r.sub[i].src == _r.sub[i + 1].src ) )
        {
            i++;
            totalCost += _r.sub[i].total;
            myCost += _r.sub[i].myCost;
            totalCalls++;
        }

        _lookup( &t, _r.sub[i].dst );

        if ( !t->seen )
        {
            /* This is a previously unseen dest, announce it */
            fprintf( _r.c, "fl=(%d) %s\nfn=(%d) %s\n0x%08x %d %ld\n", t->index, t->n->filename, t->index, t->n->function, t->n->addr, t->n->line, myCost );
            t->seen = true;
        }

        _lookup( &f, _r.sub[i].src );

        if ( !f->seen )
        {
            /* Add this in, but cost of the caller is not visible here...we need to put 1 else no code is visible */
            fprintf( _r.c, "fl=(%d) %s\nfn=(%d) %s\n0x%08x %d 1\n", f->index, f->n->filename, f->index, f->n->function, f->n->addr, f->n->line );
            f->seen = true;
        }
        else
        {
            fprintf( _r.c, "fl=(%d)\nfn=(%d)\n", f->index, f->index );
        }

        /* Now publish the call destination. By definition is is known, so can be shortformed */
        fprintf( _r.c, "cfi=(%d)\ncfn=(%d)\ncalls=%d 0x%08x %d\n", t->index, t->index, totalCalls, _r.sub[i].dst, t->n->line );
        fprintf( _r.c, "0x%08x %d %ld\n", _r.sub[i].src, f->n->line, totalCost );
    }
}
// ====================================================================================================
uint64_t _traverse( uint32_t layer )

/* Recursively traverse the calls tree, recording each subroutine call as we go along */

{
    uint32_t startPoint = _r.psn; /* Record where we came in on this iteration */
    uint64_t childCost = 0;      /* ...and keep a record of any children visited */

    /* If this is an out and we're already at the top level then it's to be ignored */
    if ( ( layer == 0 ) && ( !_r.calls[_r.psn].in ) )
    {
        _r.psn++;
        return 0;
    }

    _r.psn++; /* Move past my node... */

    /* Two cases...either an in node, in which case there is more to be covered */
    /* or an out node, in which case we're done and we can just record what we've got */

    /* ...of course there might be a whole sequence if in calls if we call several routines from ours */
    while ( _r.calls[_r.psn].in )
    {
        if ( _r.psn >= _r.cdCount - 2 )
        {
            return 0;
        }

        childCost += _traverse( layer + 1 );
    }


    /* This is my out node....they may have been others below, but this one matches my in node */
    /* At this point startPoint is the in node, and r_psn is the exit node, so store this entry */

    _r.sub = ( struct subcalls * )realloc( _r.sub, ( ++_r.subPsn ) * ( sizeof( struct subcalls ) ) );
    _r.sub[_r.subPsn - 1].dst = _r.calls[_r.psn].dst;
    _r.sub[_r.subPsn - 1].src = _r.calls[_r.psn].src;
    _r.sub[_r.subPsn - 1].total = _r.calls[_r.psn].tstamp - _r.calls[startPoint].tstamp;
    _r.sub[_r.subPsn - 1].myCost = _r.sub[_r.subPsn - 1].total - childCost;

    _r.psn++;

    /* ...and float to level above any cost we've got */
    return _r.sub[_r.subPsn - 1].total;
}
// ====================================================================================================
void _outputProfile( void )

/* Output a KCacheGrind compatible profile */

{
    _r.c = fopen( options.profile, "w" );
    fprintf( _r.c, "# callgrind format\n" );
    fprintf( _r.c, "positions: line instr\nevent: Cyc : Processor Clock Cycles\nevents: Cyc\n" );
    /* Samples are in time order, so we can determine the extent of time.... */
    fprintf( _r.c, "summary: %ld\n", _r.calls[_r.cdCount - 1].tstamp - _r.calls[0].tstamp );
    fprintf( _r.c, "ob=%s\n", options.elffile );

    /* If we have a set of sub-calls from a previous run then delete them */
    if ( _r.sub )
    {
        free( _r.sub );
        _r.sub = NULL;
    }

    _r.subPsn = 0;

    _r.psn = 0;

    while ( _r.psn < _r.cdCount - 2 )
    {
        _traverse( 0 );
    }

    _dumpProfile();
    fclose( _r.c );
}
// ====================================================================================================

/* types and compare-functions for qsort in _outputDot() */

struct callVector
{
    struct nameEntryHash *fromName;
    struct nameEntryHash *toName;
    uint32_t count;
};

static int _addr_sort_dst_fn( const void *a, const void *b )

{
    int32_t c = ( ( ( struct edge * )a )->dst ) - ( ( ( struct edge * )b )->dst );

    if ( c )
    {
        return c;
    }

    return ( ( ( struct edge * )a )->src ) - ( ( ( struct edge * )b )->src );
}

static int _calls_sort_src_fn( const void *a, const void *b )

{
    int32_t c = ( ( ( struct callVector * )a )->fromName ) - ( ( ( struct callVector * )b )->fromName );

    if ( c )
    {
        return c;
    }

    return ( ( ( struct callVector * )a )->toName ) - ( ( ( struct callVector * )b )->toName );
}

static int _calls_sort_dst_fn( const void *a, const void *b )

{
    int32_t c = ( ( ( struct callVector * )a )->toName ) - ( ( ( struct callVector * )b )->toName );

    if ( c )
    {
        return c;
    }

    return ( ( ( struct callVector * )a )->fromName ) - ( ( ( struct callVector * )b )->fromName );
}
// ====================================================================================================
void _outputDot( void )

/* Output call graph to dot file */

{
    FILE *c;
    struct nameEntryHash *f = NULL, *t = NULL;

    struct callVector *call = NULL;
    uint32_t callCount = 0;

    if ( !options.dotfile )
    {
        return;
    }

    /* Sort according to addresses visited. */
    qsort( _r.calls, _r.cdCount, sizeof( struct edge ), _addr_sort_dst_fn );

    /* Put in all the names of functions. There may be multiple addresses that match to a function, so crush them up */
    _lookup( &f, _r.calls[0].src );
    _lookup( &t, _r.calls[0].dst );

    for ( uint32_t i = 0; i < _r.cdCount; i++ )
    {
        call = ( struct callVector * )realloc( call, sizeof( struct callVector ) * ( callCount + 1 ) );
        call[callCount].fromName = f;
        call[callCount].toName = t;
        call[callCount].count = 0;

        do
        {
            call[callCount].count++;
            _lookup( &f, _r.calls[i].src );
            _lookup( &t, _r.calls[i].dst );
            i++;
        }
        while ( ( !strcmp( call[callCount].fromName->n->function, f->n->function ) ) && ( !strcmp( call[callCount].toName->n->function, t->n->function ) ) && ( i < _r.cdCount ) );

        callCount++;
    }

    c = fopen( options.dotfile, "w" );
    fprintf( c, "digraph calls\n{\n  overlap=false; splines=true; size=\"7.75,10.25\"; orientation=portrait; sep=0.1; nodesep=0.1;\n" );

    /* firstly write out the nodes in each subgraph - dest side clustered */
    qsort( call, callCount, sizeof( struct callVector ), _calls_sort_dst_fn );

    for ( uint32_t x = 1; x < callCount; x++ )
    {
        fprintf( c, "  subgraph \"cluster_%s\"\n  {\n    label=\"%s\";\n    bgcolor=lightgrey;\n", call[x - 1].toName->n->filename, call[x - 1].toName->n->filename );

        while ( x < callCount )
        {
            /* Now output each function in the subgraph */
            fprintf( c, "    %s [style=filled, fillcolor=white];\n", call[x - 1].toName->n->function );

            /* Spin forwards until the function name _or_ filename changes */
            while ( ( x < callCount ) && ( call[x - 1].toName == call[x].toName ) )
            {
                x++;
            }

            if ( ( x >= callCount ) || ( strcmp( call[x - 1].toName->n->filename, call[x].toName->n->filename ) ) )
            {
                break;
            }

            x++;
        }

        fprintf( c, "  }\n\n" );
    }

    /* now write out the nodes in each subgraph - source side clustered */
    qsort( call, callCount, sizeof( struct callVector ), _calls_sort_src_fn );

    for ( uint32_t x = 1; x < callCount; x++ )
    {
        fprintf( c, "  subgraph \"cluster_%s\"\n  {\n    label=\"%s\";\n    bgcolor=lightgrey;\n", call[x - 1].fromName->n->filename, call[x - 1].fromName->n->filename );

        while ( x < callCount )
        {
            /* Now output each function in the subgraph */
            fprintf( c, "    %s [style=filled, fillcolor=white];\n", call[x - 1].fromName->n->function );

            /* Spin forwards until the function name _or_ filename changes */
            while ( ( x < callCount ) && ( call[x - 1].fromName == call[x].fromName ) )
            {
                x++;
            }

            if ( ( x >= callCount ) || ( strcmp( call[x - 1].fromName->n->filename, call[x].fromName->n->filename ) ) )
            {
                break;
            }

            x++;
        }

        fprintf( c, "  }\n\n" );
    }

    /* Now go through and label the arrows... */
    for ( uint32_t x = 0; x < callCount; x++ )
    {
        fprintf( c, "    %s -> ", call[x].fromName->n->function );
        fprintf( c, "%s [label=%d , weight=0.1;];\n", call[x].toName->n->function, call[x].count );
    }

    fprintf( c, "}\n" );
    fclose( c );
}
// ====================================================================================================
void _handlePCSample( struct ITMDecoder *i, struct ITMPacket *p )

{
    return;
}
// ====================================================================================================
void _handleHW( struct ITMDecoder *i )

{
    struct ITMPacket p;
    ITMGetPacket( i, &p );

    switch ( p.srcAddr )
    {
        // --------------
        case 0: /* DWT Event */
            break;

        // --------------
        case 1: /* Exception */
            break;

        // --------------
        case 2: /* PC Counter Sample */
            _handlePCSample( i, &p );
            break;

        // --------------
        default:
            break;
            // --------------
    }
}
// ====================================================================================================
void _itmPumpProcess( char c )

/* Handle individual characters into the itm decoder */

{
    switch ( ITMPump( &_r.i, c ) )
    {
        // ------------------------------------
        case ITM_EV_NONE:
            break;

        // ------------------------------------
        case ITM_EV_UNSYNCED:
            genericsReport( V_INFO, "ITM Lost Sync (%d)" EOL, ITMDecoderGetStats( &_r.i )->lostSyncCount );
            break;

        // ------------------------------------
        case ITM_EV_SYNCED:
            genericsReport( V_INFO, "ITM In Sync (%d)" EOL, ITMDecoderGetStats( &_r.i )->syncCount );
            break;

        // ------------------------------------
        case ITM_EV_RESERVED_PACKET_RXED:
            genericsReport( V_INFO, "Reserved Packet Received" EOL );
            break;


        // ------------------------------------
        case ITM_EV_XTN_PACKET_RXED:
            genericsReport( V_INFO, "Unknown Extension Packet Received" EOL );
            break;

        // ------------------------------------
        case ITM_EV_OVERFLOW:
            genericsReport( V_WARN, "ITM Overflow (%d)" EOL, ITMDecoderGetStats( &_r.i )->overflow );
            break;

        // ------------------------------------
        case ITM_EV_ERROR:
            genericsReport( V_WARN, "ITM Error" EOL );
            break;

        // ------------------------------------
        case ITM_EV_TS_PACKET_RXED:
            break;

        // ------------------------------------
        case ITM_EV_SW_PACKET_RXED:
            _handleSW( &_r.i );
            break;

        // ------------------------------------
        case ITM_EV_HW_PACKET_RXED:
            _handleHW( &_r.i );
            break;

            // ------------------------------------
    }
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Protocol pump for decoding messages
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void _protocolPump( uint8_t c )

/* Top level protocol pump */

{
    if ( options.useTPIU )
    {
        switch ( TPIUPump( &_r.t, c ) )
        {
            // ------------------------------------
            case TPIU_EV_NEWSYNC:
                genericsReport( V_INFO, "TPIU In Sync (%d)" EOL, TPIUDecoderGetStats( &_r.t )->syncCount );

            case TPIU_EV_SYNCED:
                ITMDecoderForceSync( &_r.i, true );
                break;

            // ------------------------------------
            case TPIU_EV_RXING:
            case TPIU_EV_NONE:
                break;

            // ------------------------------------
            case TPIU_EV_UNSYNCED:
                genericsReport( V_INFO, "TPIU Lost Sync (%d)" EOL, TPIUDecoderGetStats( &_r.t )->lostSync );
                ITMDecoderForceSync( &_r.i, false );
                break;

            // ------------------------------------
            case TPIU_EV_RXEDPACKET:
                if ( !TPIUGetPacket( &_r.t, &_r.p ) )
                {
                    genericsReport( V_WARN, "TPIUGetPacket fell over" EOL );
                }

                for ( uint32_t g = 0; g < _r.p.len; g++ )
                {
                    if ( _r.p.packet[g].s == options.tpiuITMChannel )
                    {
                        _itmPumpProcess( _r.p.packet[g].d );
                        continue;
                    }

                    if ( _r.p.packet[g].s != 0 )
                    {
                        genericsReport( V_WARN, "Unknown TPIU channel %02x" EOL, _r.p.packet[g].s );
                    }
                }

                break;

            // ------------------------------------
            case TPIU_EV_ERROR:
                genericsReport( V_WARN, "****ERROR****" EOL );
                break;
                // ------------------------------------
        }
    }
    else
    {
        /* There's no TPIU in use, so this goes straight to the ITM layer */
        _itmPumpProcess( c );
    }
}
// ====================================================================================================
void _printHelp( char *progName )

{
    fprintf( stdout, "Usage: %s <htv> <-e ElfFile> <-m MaxHistory> -r <routines> <-i channel> <-p port> <-s server>" EOL, progName );
    fprintf( stdout, "       d: <DeleteMaterial> to take off front of filenames" EOL );
    fprintf( stdout, "       e: <ElfFile> to use for symbols" EOL );
    fprintf( stdout, "       f: <FileChannel> for file writing (default %d)" EOL, options.fileChannel );
    fprintf( stdout, "       g: <TraceChannel> for trace output (default %d)" EOL, options.traceChannel );
    fprintf( stdout, "       h: This help" EOL );
    fprintf( stdout, "       i: <channel> Set ITM Channel in TPIU decode (defaults to 1)" EOL );
    fprintf( stdout, "       l: Aggregate per line rather than per function" EOL );
    fprintf( stdout, "       n: Enforce sync requirement for ITM (i.e. ITM needs to issue syncs)" EOL );
    fprintf( stdout, "       s: <Server>:<Port> to use" EOL );
    fprintf( stdout, "       t: Use TPIU decoder" EOL );
    fprintf( stdout, "       v: <level> Verbose mode 0(errors)..3(debug)" EOL );
    fprintf( stdout, "       y: <Filename> dotty filename for structured callgraph output" EOL );
    fprintf( stdout, "       z: <Filename> profile filename for kcachegrind output" EOL );
}
// ====================================================================================================
int _processOptions( int argc, char *argv[] )

{
    int c;

    while ( ( c = getopt ( argc, argv, "d:e:f:g:hi:lnp:s:tvy:z:" ) ) != -1 )

        switch ( c )
        {
            // ------------------------------------
            case 'd':
                options.deleteMaterial = optarg;
                break;

            // ------------------------------------
            case 'e':
                options.elffile = optarg;
                break;

            // ------------------------------------
            case 'f':
                options.fileChannel = atoi( optarg );
                break;

            // ------------------------------------
            case 'g':
                options.traceChannel = atoi( optarg );
                break;

            // ------------------------------------
            case 'h':
                _printHelp( argv[0] );
                exit( 0 );

            // ------------------------------------
            case 'i':
                options.tpiuITMChannel = atoi( optarg );
                break;

            // ------------------------------------
            case 'n':
                options.forceITMSync = false;
                break;

            // ------------------------------------
            case 's':
                options.server = optarg;

                // See if we have an optional port number too
                char *a = optarg;

                while ( ( *a ) && ( *a != ':' ) )
                {
                    a++;
                }

                if ( *a == ':' )
                {
                    *a = 0;
                    options.port = atoi( ++a );
                }

                if ( !options.port )
                {
                    options.port = SERVER_PORT;
                }

                break;

            // ------------------------------------
            case 't':
                options.useTPIU = true;
                break;

            // ------------------------------------
            case 'v':
                genericsSetReportLevel( atoi( optarg ) );
                break;

            // ------------------------------------
            case 'y':
                options.dotfile = optarg;
                break;

            // ------------------------------------
            case 'z':
                options.profile = optarg;
                break;

            // ------------------------------------
            case '?':
                if ( optopt == 'b' )
                {
                    genericsReport( V_ERROR, "Option '%c' requires an argument." EOL, optopt );
                }
                else if ( !isprint ( optopt ) )
                {
                    genericsReport( V_ERROR, "Unknown option character `\\x%x'." EOL, optopt );
                }

                return false;

            // ------------------------------------
            default:
                genericsReport( V_ERROR, "Unknown option %c" EOL, optopt );
                return false;
                // ------------------------------------
        }

    if ( ( options.useTPIU ) && ( !options.tpiuITMChannel ) )
    {
        genericsReport( V_ERROR, "TPIU set for use but no channel set for ITM output" EOL );
        return false;
    }

    if ( !options.elffile )
    {
        genericsReport( V_ERROR, "Elf File not specified" EOL );
        exit( -2 );
    }

    genericsReport( V_INFO, "orbtop V" VERSION " (Git %08X %s, Built " BUILD_DATE ")" EOL, GIT_HASH, ( GIT_DIRTY ? "Dirty" : "Clean" ) );

    genericsReport( V_INFO, "Server        : %s:%d" EOL, options.server, options.port );
    genericsReport( V_INFO, "Delete Mat    : %s" EOL, options.deleteMaterial ? options.deleteMaterial : "None" );
    genericsReport( V_INFO, "Elf File      : %s" EOL, options.elffile );
    genericsReport( V_INFO, "DOT file      : %s" EOL, options.dotfile ? options.dotfile : "None" );
    genericsReport( V_INFO, "ForceSync     : %s" EOL, options.forceITMSync ? "true" : "false" );
    genericsReport( V_INFO, "Trace/File Ch : %d/%d" EOL, options.traceChannel, options.fileChannel );

    if ( options.useTPIU )
    {
        genericsReport( V_INFO, "Using TPIU  : true (ITM on channel %d)" EOL, options.tpiuITMChannel );
    }

    return true;
}
// ====================================================================================================
int main( int argc, char *argv[] )

{
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    uint8_t cbw[TRANSFER_SIZE];
    uint64_t lastTime;

    ssize_t t;
    int flag = 1;

    /* Fill in a time to start from */
    lastTime = _timestamp();

    if ( !_processOptions( argc, argv ) )
    {
        exit( -1 );
    }

    /* Get the symbols from file */
    _r.s = SymbolSetCreate( options.elffile );

    if ( !_r.s )
    {
        exit( -3 );
    }

    /* Reset the TPIU handler before we start */
    TPIUDecoderInit( &_r.t );
    ITMDecoderInit( &_r.i, options.forceITMSync );

    sockfd = socket( AF_INET, SOCK_STREAM, 0 );
    setsockopt( sockfd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof( flag ) );

    if ( sockfd < 0 )
    {
        perror( "Error creating socket\n" );
        return -1;
    }

    /* Now open the network connection */
    bzero( ( char * ) &serv_addr, sizeof( serv_addr ) );
    server = gethostbyname( options.server );

    if ( !server )
    {
        perror( "Cannot find host" );
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    bcopy( ( char * )server->h_addr,
           ( char * )&serv_addr.sin_addr.s_addr,
           server->h_length );
    serv_addr.sin_port = htons( options.port );

    if ( connect( sockfd, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr ) ) < 0 )
    {
        perror( "Could not connect" );
        return -1;
    }

    while ( ( t = read( sockfd, cbw, TRANSFER_SIZE ) ) > 0 )
    {
        uint8_t *c = cbw;

        while ( t-- )
        {
            _protocolPump( *c++ );
        }

        if ( _timestamp() - lastTime > TOP_UPDATE_INTERVAL )
        {
            lastTime = _timestamp();

            if (  _r.cdCount )
            {
                if ( options.dotfile )
                {
                    _outputDot();
                }

                if ( options.profile )
                {
                    _outputProfile();
                }
            }

            genericsReport( V_INFO, "%d records processed" EOL, _r.cdCount );
            /* Now free up this seconds data */
            free( _r.calls );
            _r.calls = NULL;
            _r.cdCount = 0;

            if ( !SymbolSetCheckValidity( &_r.s, options.elffile ) )
            {
                _flushHash();

                genericsReport( V_INFO, "Reload %s" EOL, options.elffile );

                if ( !_r.s )
                {
                    /* Its possible the file was in the process of being written, so wait before testing again */
                    usleep( 1000000 );

                    if ( !SymbolSetCheckValidity( &_r.s, options.elffile ) )
                    {
                        genericsReport( V_ERROR, "Elf file was lost" EOL );
                        return -1;
                    }
                }
            }
        }

        /* Check to make sure there's not an unexpected TPIU in here */
        if ( ITMDecoderGetStats( &_r.i )->tpiuSyncCount )
        {
            genericsReport( V_WARN, "Got a TPIU sync while decoding ITM...did you miss a -t option?" EOL );
            break;
        }
    }

    if ( ( !ITMDecoderGetStats( &_r.i )->tpiuSyncCount ) )
    {
        genericsReport( V_ERROR, "Read failed" EOL );
    }

    close( sockfd );
    return -2;
}
// ====================================================================================================
