/**
    bambam
    Copyright (C) 2009-2013 German Tischler
    Copyright (C) 2011-2013 Genome Research Limited

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**/
#include <biobambam/BamBamConfig.hpp>
#include <biobambam/Licensing.hpp>

#include <iomanip>

#include <config.h>

#include <libmaus/bambam/BamDecoder.hpp>
#include <libmaus/bambam/BamWriter.hpp>
#include <libmaus/bambam/BamParallelRewrite.hpp>
#include <libmaus/bambam/ProgramHeaderLineSet.hpp>
#include <libmaus/util/ArgInfo.hpp>
#include <libmaus/util/MemUsage.hpp>

::libmaus::bambam::BamHeader::unique_ptr_type updateHeader(
	::libmaus::util::ArgInfo const & arginfo,
	::libmaus::bambam::BamHeader const & header
)
{
	std::string const headertext(header.text);

	// add PG line to header
	std::string const upheadtext = ::libmaus::bambam::ProgramHeaderLineSet::addProgramLine(
		headertext,
		"bamfilterflags", // ID
		"bamfilterflags", // PN
		arginfo.commandline, // CL
		::libmaus::bambam::ProgramHeaderLineSet(headertext).getLastIdInChain(), // PP
		std::string(PACKAGE_VERSION) // VN			
	);
	// construct new header
	::libmaus::bambam::BamHeader::unique_ptr_type uphead(new ::libmaus::bambam::BamHeader(upheadtext));
	
	return UNIQUE_PTR_MOVE(uphead);
}

struct UpdateHeader : public libmaus::bambam::BamHeaderRewriteCallback
{
	libmaus::util::ArgInfo const & arginfo;

	UpdateHeader(libmaus::util::ArgInfo const & rarginfo)
	: arginfo(rarginfo)
	{
	
	}

	::libmaus::bambam::BamHeader::unique_ptr_type operator()(::libmaus::bambam::BamHeader const & header)  const
	{
		return UNIQUE_PTR_MOVE(updateHeader(arginfo,header));
	}
};

int bamfilterflags(::libmaus::util::ArgInfo const & arginfo)
{
	uint32_t const excludeflags = libmaus::bambam::BamFlagBase::stringToFlags(arginfo.getValue<std::string>("exclude",""));

	std::cerr << "[V] excluding " << excludeflags << std::endl;

	int const level = arginfo.getValue<int>("level",Z_DEFAULT_COMPRESSION);
	
	switch ( level )
	{
		case Z_NO_COMPRESSION:
		case Z_BEST_SPEED:
		case Z_BEST_COMPRESSION:
		case Z_DEFAULT_COMPRESSION:
			break;
		default:
		{
			::libmaus::exception::LibMausException se;
			se.getStream()
				<< "Unknown compression level, please use"
				<< " level=" << Z_DEFAULT_COMPRESSION << " (default) or"
				<< " level=" << Z_BEST_SPEED << " (fast) or"
				<< " level=" << Z_BEST_COMPRESSION << " (best) or"
				<< " level=" << Z_NO_COMPRESSION << " (no compression)" << std::endl;
			se.finish();
			throw se;
		}
			break;
	}

	uint64_t const numthreads = arginfo.getValue<uint64_t>("numthreads",1);
	uint64_t cnt = 0;
	uint64_t kept = 0;

	if ( numthreads == 1 )
	{
		::libmaus::bambam::BamDecoder BD(std::cin);
		::libmaus::bambam::BamHeader const & bamheader = BD.getHeader();
		::libmaus::bambam::BamAlignment & alignment = BD.getAlignment();
		::libmaus::bambam::BamWriter writer(std::cout,bamheader,level);
	
		for ( ; BD.readAlignment(); ++cnt )
		{
			if ( cnt % (1024*1024) == 0 )
				std::cerr << "[V] processed " << cnt << " kept " << kept << " removed " << (cnt-kept) << std::endl;
			if ( ! (alignment.getFlags() & excludeflags) )			
			{
				alignment.serialise(writer.getStream());
				++kept;
			}
		}

		std::cerr << "[V] " << cnt << std::endl;	
	}
	else
	{
		UpdateHeader UH(arginfo);
		libmaus::bambam::BamParallelRewrite BPR(std::cin,UH,std::cout,Z_DEFAULT_COMPRESSION,numthreads,4 /* blocks per thread */);
		libmaus::bambam::BamAlignmentDecoder & dec = BPR.getDecoder();
		libmaus::bambam::BamParallelRewrite::writer_type & writer = BPR.getWriter();

		libmaus::bambam::BamAlignment const & algn = dec.getAlignment();
		for ( ; dec.readAlignment(); ++cnt )
		{
			if ( cnt % (1024*1024) == 0 )
				std::cerr << "[V] processed " << cnt << " kept " << kept << " removed " << (cnt-kept) << std::endl;
			if ( ! (algn.getFlags() & excludeflags) )
			{
				algn.serialise(writer.getStream());	
				++kept;
			}
		}
		
		std::cerr << "[V] " << cnt << std::endl;	
	}
	
	std::cerr << "[V] kept " << kept << " removed " << cnt-kept << std::endl;

	return EXIT_SUCCESS;
}

int main(int argc, char * argv[])
{
	try
	{
		libmaus::timing::RealTimeClock rtc; rtc.start();
		
		::libmaus::util::ArgInfo const arginfo(argc,argv);
		
		for ( uint64_t i = 0; i < arginfo.restargs.size(); ++i )
			if ( 
				arginfo.restargs[i] == "-v"
				||
				arginfo.restargs[i] == "--version"
			)
			{
				std::cerr << ::biobambam::Licensing::license();
				return EXIT_SUCCESS;
			}
			else if ( 
				arginfo.restargs[i] == "-h"
				||
				arginfo.restargs[i] == "--help"
			)
			{
				std::cerr << ::biobambam::Licensing::license() << std::endl;
				std::cerr << "Key=Value pairs:" << std::endl;
				std::cerr << std::endl;
				
				std::vector< std::pair<std::string,std::string> > V;
				
				V.push_back ( std::pair<std::string,std::string> ( "level=<[-1]>", "zlib compression setting for output file (0=uncompressed,1=fast,9=best,-1=default)" ) );
				V.push_back ( std::pair<std::string,std::string> ( "exclude=<[]>", "exclude alignments matching any of the given flags" ) );
				V.push_back ( std::pair<std::string,std::string> ( "numthreads=<[1]>", "number of recoding threads" ) );
				
				::biobambam::Licensing::printMap(std::cerr,V);

				std::cerr << std::endl;
				std::cerr << "Alignment flags: PAIRED,PROPER_PAIR,UNMAP,MUNMAP,REVERSE,MREVERSE,READ1,READ2,SECONDARY,QCFAIL,DUP" << std::endl;

				std::cerr << std::endl;
				return EXIT_SUCCESS;
			}
		
			
		bamfilterflags(arginfo);
		
		std::cerr << "[V] " << libmaus::util::MemUsage() << " wall clock time " << rtc.formatTime(rtc.getElapsedSeconds()) << std::endl;

	}
	catch(std::exception const & ex)
	{
		std::cerr << ex.what() << std::endl;
		return EXIT_FAILURE;
	}
}