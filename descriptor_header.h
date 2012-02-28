/*
 * descriptor_header.h
 *
 *  Created on: Feb 25, 2012
 *      Author: drewkutilek
 */

#ifndef DESCRIPTOR_HEADER_H_
#define DESCRIPTOR_HEADER_H_

#define HEADER_SIZE 23

enum header_type {
	ping, pong, query, quieryHit, push
};
class DescriptorHeader;

#endif /* DESCRIPTOR_HEADER_H_ */
