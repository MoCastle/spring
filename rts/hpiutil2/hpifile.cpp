/*
 * hpifile.cpp
 * hpi file class implementation
 * Copyright (C) 2005 Christopher Han <xiphux@gmail.com>
 *
 * This file is part of hpiutil2.
 *
 * hpiutil2 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * hpiutil2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with hpiutil2; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdexcept>
#include <sstream>
#include <iostream>
#include "hpifile.h"

/**
 * Constructor
 * @param fname path to target hpi file as a c string
 */
hpifile::hpifile(const char *fname)
{
	file = new scrambledfile(fname);
	validate(fname);
}

/**
 * Constructor
 * @param fname path to target hpi file as a c++ string
 */
hpifile::hpifile(std::string const &fname)
{
	file = new scrambledfile(fname);
	validate(fname.c_str());
}

/**
 * Destructor
 */
hpifile::~hpifile()
{
	delete file;
}

/**
 * validate()
 * Reads the file's headers and ensures it is a legitimate hpi file
 * also sets decryption key if present
 * @param n path to target hpi
 */
void hpifile::validate(const char *n)
{
	valid = false;
	header_hapimagic = file->readint();
	if (header_hapimagic != HAPI_MAGIC) {
		std::cerr << "File " << n << ": Invalid HAPI signature: 0x" << std::hex << header_hapimagic << std::endl;
		return;
	}
	header_bankmagic = file->readint();
	if (header_bankmagic != HAPI_VERSION_MAGIC) {
		if (header_bankmagic == BANK_MAGIC)
			std::cerr << "File " << n << ": Bank subtype signature looks like a saved game: 0x" << std::hex << header_bankmagic << std::endl;
		else if (header_bankmagic == HAPI2_VERSION_MAGIC)
			std::cerr << "File " << n << ": HAPIv2 files not supported yet" << std::endl;
		else
			std::cerr << "File " << n << ": Invalid bank subtype signature: 0x" << std::hex << header_bankmagic << std::endl;
		return;
	}
	header_offset = file->readint();
	header_key = file->readint();
	header_diroffset = file->readint();
	file->setkey(header_key);
	valid = true;
	flatlist.push_back(&dirinfo("","",header_diroffset));
}

/**
 * dirinfo()
 * creates an hpientry object representing a given directory
 * @return hpientry object for the directory
 * @param parentname name of this object's parent (if applicable)
 * @param dirname name of this directory
 * @param offset offset in hpi file
 */
hpientry& hpifile::dirinfo(std::string const &parentname, std::string const &dirname, const uint32_t offset)
{
	std::vector<hpientry*> listing;
	std::string newparent;
	if (parentname=="")
		newparent = dirname;
	else
		newparent = parentname+PATHSEPARATOR+dirname;
	file->seek(offset);
	uint32_t entries = file->readint();
	uint32_t unknown = file->readint();
	for (int i = 0; i < entries; i++) {
		uint32_t nameoffset = file->readint();
		uint32_t infooffset = file->readint();
		uint8_t entrytype = file->read();
		uint32_t currentpos = file->file.tellg();
		file->seek(nameoffset);
		std::string itemname = file->readstring();
		file->seek(infooffset);
		switch (entrytype) {
			case 0:
				listing.push_back(&(fileinfo(newparent,itemname,infooffset)));
				break;
			case 1:
				listing.push_back(&(dirinfo(newparent,itemname,infooffset)));
				break;
			default:
				throw std::runtime_error("Unknown entry type");
				break;
		}
		file->seek(currentpos);
	}
	hpientry *ret = new hpientry(*this,parentname,dirname,(uint32_t)0,(uint32_t)0);
	ret->directory = true;
	ret->subdir = listing;
	flatlist.push_back(ret);
	return *ret;
}

/**
 * fileinfo()
 * creates an hpientry object representing a given file
 * @return hpientry object for the file
 * @param parentname name of this object's parent (if applicable)
 * @param name name of the file
 * @param offset offset in hpi file
 */
hpientry& hpifile::fileinfo(std::string const &parentname, std::string const &name, const uint32_t offset)
{
	uint32_t doff = file->readint();
	uint32_t dsize = file->readint();
	hpientry *ret = new hpientry(*this,parentname,name,doff,dsize);
	flatlist.push_back(ret);
	return *ret;
}

/**
 * getdata()
 * load a file's data into a buffer.
 * @return the number of bytes read
 * @param he hpientry for the target file
 * @param data buffer to read data into
 */
uint32_t hpifile::getdata(hpientry const &he, uint8_t *data)
{
	if (he.file != this) {
		std::cerr << "HPIentry does not match this HPIfile" << std::endl;
		return 0;
	}
	if (he.directory) {
		std::cerr << "HPIentry is a directory, not a file" << std::endl;
		return 0;
	}
	uint32_t chunknum = bitdiv(he.size,16) + (bitmod(he.size,16)?1:0);
	uint32_t *chunksizes = (uint32_t*)calloc(chunknum,sizeof(uint32_t));
	file->seek(he.offset);
	for (int i = 0; i < chunknum; i++)
		chunksizes[i] = file->readint();
	uint32_t chunkoffset = he.offset + (chunknum * 4);
	int j = 0;
	for (int i = 0; i < chunknum; i++) {
		uint32_t chunksize = chunksizes[i];
		substream *ss = new substream(*file,(chunkoffset + j),chunksize);
		sqshstream *sqsh = new sqshstream(*ss);
		if (sqsh->valid) {
			j += sqsh->readall(&data[j]);
			delete sqsh;
			delete ss;
		} else {
			delete sqsh;
			delete ss;
			free(chunksizes);
			return 0;
		}
	}
	free(chunksizes);
	return j;
}
