/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

package thrift

import (
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"math"
)

const (
	COMPACT_PROTOCOL_ID       = 0x082
	COMPACT_VERSION           = 1
	COMPACT_VERSION_MASK      = 0x1f
	COMPACT_TYPE_MASK         = 0x0E0
	COMPACT_TYPE_BITS         = 0x07
	COMPACT_TYPE_SHIFT_AMOUNT = 5
)

type tCompactType byte

const (
	COMPACT_BOOLEAN_TRUE  = 0x01
	COMPACT_BOOLEAN_FALSE = 0x02
	COMPACT_BYTE          = 0x03
	COMPACT_I16           = 0x04
	COMPACT_I32           = 0x05
	COMPACT_I64           = 0x06
	COMPACT_DOUBLE        = 0x07
	COMPACT_BINARY        = 0x08
	COMPACT_LIST          = 0x09
	COMPACT_SET           = 0x0A
	COMPACT_MAP           = 0x0B
	COMPACT_STRUCT        = 0x0C
	COMPACT_UUID          = 0x0D
)

var (
	ttypeToCompactType map[TType]tCompactType
)

func init() {
	ttypeToCompactType = map[TType]tCompactType{
		STOP:   STOP,
		BOOL:   COMPACT_BOOLEAN_TRUE,
		BYTE:   COMPACT_BYTE,
		I16:    COMPACT_I16,
		I32:    COMPACT_I32,
		I64:    COMPACT_I64,
		DOUBLE: COMPACT_DOUBLE,
		STRING: COMPACT_BINARY,
		LIST:   COMPACT_LIST,
		SET:    COMPACT_SET,
		MAP:    COMPACT_MAP,
		STRUCT: COMPACT_STRUCT,
		UUID:   COMPACT_UUID,
	}
}

type TCompactProtocolFactory struct {
	cfg *TConfiguration
}

// Deprecated: Use NewTCompactProtocolFactoryConf instead.
func NewTCompactProtocolFactory() *TCompactProtocolFactory {
	return NewTCompactProtocolFactoryConf(&TConfiguration{
		noPropagation: true,
	})
}

func NewTCompactProtocolFactoryConf(conf *TConfiguration) *TCompactProtocolFactory {
	return &TCompactProtocolFactory{
		cfg: conf,
	}
}

func (p *TCompactProtocolFactory) GetProtocol(trans TTransport) TProtocol {
	return NewTCompactProtocolConf(trans, p.cfg)
}

func (p *TCompactProtocolFactory) SetTConfiguration(conf *TConfiguration) {
	p.cfg = conf
}

type TCompactProtocol struct {
	trans         TRichTransport
	origTransport TTransport

	cfg *TConfiguration

	// Used to keep track of the last field for the current and previous structs,
	// so we can do the delta stuff.
	lastField   []int
	lastFieldId int

	// If we encounter a boolean field begin, save the TField here so it can
	// have the value incorporated.
	booleanFieldName    string
	booleanFieldId      int16
	booleanFieldPending bool

	// If we read a field header, and it's a boolean field, save the boolean
	// value here so that readBool can use it.
	boolValue          bool
	boolValueIsNotNull bool
	buffer             [64]byte
}

// Deprecated: Use NewTCompactProtocolConf instead.
func NewTCompactProtocol(trans TTransport) *TCompactProtocol {
	return NewTCompactProtocolConf(trans, &TConfiguration{
		noPropagation: true,
	})
}

func NewTCompactProtocolConf(trans TTransport, conf *TConfiguration) *TCompactProtocol {
	PropagateTConfiguration(trans, conf)
	p := &TCompactProtocol{
		origTransport: trans,
		cfg:           conf,
	}
	if et, ok := trans.(TRichTransport); ok {
		p.trans = et
	} else {
		p.trans = NewTRichTransport(trans)
	}

	return p
}

//
// Public Writing methods.
//

// Write a message header to the wire. Compact Protocol messages contain the
// protocol version so we can migrate forwards in the future if need be.
func (p *TCompactProtocol) WriteMessageBegin(ctx context.Context, name string, typeId TMessageType, seqid int32) error {
	err := p.writeByteDirect(COMPACT_PROTOCOL_ID)
	if err != nil {
		return NewTProtocolException(err)
	}
	err = p.writeByteDirect((COMPACT_VERSION & COMPACT_VERSION_MASK) | ((byte(typeId) << COMPACT_TYPE_SHIFT_AMOUNT) & COMPACT_TYPE_MASK))
	if err != nil {
		return NewTProtocolException(err)
	}
	_, err = p.writeVarint32(seqid)
	if err != nil {
		return NewTProtocolException(err)
	}
	e := p.WriteString(ctx, name)
	return e

}

func (p *TCompactProtocol) WriteMessageEnd(ctx context.Context) error { return nil }

// Write a struct begin. This doesn't actually put anything on the wire. We
// use it as an opportunity to put special placeholder markers on the field
// stack so we can get the field id deltas correct.
func (p *TCompactProtocol) WriteStructBegin(ctx context.Context, name string) error {
	p.lastField = append(p.lastField, p.lastFieldId)
	p.lastFieldId = 0
	return nil
}

// Write a struct end. This doesn't actually put anything on the wire. We use
// this as an opportunity to pop the last field from the current struct off
// of the field stack.
func (p *TCompactProtocol) WriteStructEnd(ctx context.Context) error {
	if len(p.lastField) <= 0 {
		return NewTProtocolExceptionWithType(INVALID_DATA, errors.New("WriteStructEnd called without matching WriteStructBegin call before"))
	}
	p.lastFieldId = p.lastField[len(p.lastField)-1]
	p.lastField = p.lastField[:len(p.lastField)-1]
	return nil
}

func (p *TCompactProtocol) WriteFieldBegin(ctx context.Context, name string, typeId TType, id int16) error {
	if typeId == BOOL {
		// we want to possibly include the value, so we'll wait.
		p.booleanFieldName, p.booleanFieldId, p.booleanFieldPending = name, id, true
		return nil
	}
	_, err := p.writeFieldBeginInternal(ctx, name, typeId, id, 0xFF)
	return NewTProtocolException(err)
}

// The workhorse of writeFieldBegin. It has the option of doing a
// 'type override' of the type header. This is used specifically in the
// boolean field case.
func (p *TCompactProtocol) writeFieldBeginInternal(ctx context.Context, name string, typeId TType, id int16, typeOverride byte) (int, error) {
	// short lastField = lastField_.pop();

	// if there's a type override, use that.
	var typeToWrite byte
	if typeOverride == 0xFF {
		typeToWrite = byte(p.getCompactType(typeId))
	} else {
		typeToWrite = typeOverride
	}
	// check if we can use delta encoding for the field id
	fieldId := int(id)
	written := 0
	if fieldId > p.lastFieldId && fieldId-p.lastFieldId <= 15 {
		// write them together
		err := p.writeByteDirect(byte((fieldId-p.lastFieldId)<<4) | typeToWrite)
		if err != nil {
			return 0, err
		}
	} else {
		// write them separate
		err := p.writeByteDirect(typeToWrite)
		if err != nil {
			return 0, err
		}
		err = p.WriteI16(ctx, id)
		written = 1 + 2
		if err != nil {
			return 0, err
		}
	}

	p.lastFieldId = fieldId
	return written, nil
}

func (p *TCompactProtocol) WriteFieldEnd(ctx context.Context) error { return nil }

func (p *TCompactProtocol) WriteFieldStop(ctx context.Context) error {
	err := p.writeByteDirect(STOP)
	return NewTProtocolException(err)
}

func (p *TCompactProtocol) WriteMapBegin(ctx context.Context, keyType TType, valueType TType, size int) error {
	if size == 0 {
		err := p.writeByteDirect(0)
		return NewTProtocolException(err)
	}
	_, err := p.writeVarint32(int32(size))
	if err != nil {
		return NewTProtocolException(err)
	}
	err = p.writeByteDirect(byte(p.getCompactType(keyType))<<4 | byte(p.getCompactType(valueType)))
	return NewTProtocolException(err)
}

func (p *TCompactProtocol) WriteMapEnd(ctx context.Context) error { return nil }

// Write a list header.
func (p *TCompactProtocol) WriteListBegin(ctx context.Context, elemType TType, size int) error {
	_, err := p.writeCollectionBegin(elemType, size)
	return NewTProtocolException(err)
}

func (p *TCompactProtocol) WriteListEnd(ctx context.Context) error { return nil }

// Write a set header.
func (p *TCompactProtocol) WriteSetBegin(ctx context.Context, elemType TType, size int) error {
	_, err := p.writeCollectionBegin(elemType, size)
	return NewTProtocolException(err)
}

func (p *TCompactProtocol) WriteSetEnd(ctx context.Context) error { return nil }

func (p *TCompactProtocol) WriteBool(ctx context.Context, value bool) error {
	v := byte(COMPACT_BOOLEAN_FALSE)
	if value {
		v = byte(COMPACT_BOOLEAN_TRUE)
	}
	if p.booleanFieldPending {
		// we haven't written the field header yet
		_, err := p.writeFieldBeginInternal(ctx, p.booleanFieldName, BOOL, p.booleanFieldId, v)
		p.booleanFieldPending = false
		return NewTProtocolException(err)
	}
	// we're not part of a field, so just write the value.
	err := p.writeByteDirect(v)
	return NewTProtocolException(err)
}

// Write a byte. Nothing to see here!
func (p *TCompactProtocol) WriteByte(ctx context.Context, value int8) error {
	err := p.writeByteDirect(byte(value))
	return NewTProtocolException(err)
}

// Write an I16 as a zigzag varint.
func (p *TCompactProtocol) WriteI16(ctx context.Context, value int16) error {
	_, err := p.writeVarint32(p.int32ToZigzag(int32(value)))
	return NewTProtocolException(err)
}

// Write an i32 as a zigzag varint.
func (p *TCompactProtocol) WriteI32(ctx context.Context, value int32) error {
	_, err := p.writeVarint32(p.int32ToZigzag(value))
	return NewTProtocolException(err)
}

// Write an i64 as a zigzag varint.
func (p *TCompactProtocol) WriteI64(ctx context.Context, value int64) error {
	_, err := p.writeVarint64(p.int64ToZigzag(value))
	return NewTProtocolException(err)
}

// Write a double to the wire as 8 bytes.
func (p *TCompactProtocol) WriteDouble(ctx context.Context, value float64) error {
	buf := p.buffer[0:8]
	binary.LittleEndian.PutUint64(buf, math.Float64bits(value))
	_, err := p.trans.Write(buf)
	return NewTProtocolException(err)
}

// Write a string to the wire with a varint size preceding.
func (p *TCompactProtocol) WriteString(ctx context.Context, value string) error {
	_, e := p.writeVarint32(int32(len(value)))
	if e != nil {
		return NewTProtocolException(e)
	}
	if len(value) == 0 {
		return nil
	}
	_, e = p.trans.WriteString(value)
	return e
}

// Write a byte array, using a varint for the size.
func (p *TCompactProtocol) WriteBinary(ctx context.Context, bin []byte) error {
	_, e := p.writeVarint32(int32(len(bin)))
	if e != nil {
		return NewTProtocolException(e)
	}
	if len(bin) > 0 {
		_, e = p.trans.Write(bin)
		return NewTProtocolException(e)
	}
	return nil
}

// Write a Tuuid to the wire as 16 bytes.
func (p *TCompactProtocol) WriteUUID(ctx context.Context, value Tuuid) error {
	_, err := p.trans.Write(value[:])
	return NewTProtocolException(err)
}

//
// Reading methods.
//

// Read a message header.
func (p *TCompactProtocol) ReadMessageBegin(ctx context.Context) (name string, typeId TMessageType, seqId int32, err error) {
	var protocolId byte

	_, deadlineSet := ctx.Deadline()
	for {
		protocolId, err = p.readByteDirect()
		if deadlineSet && isTimeoutError(err) && ctx.Err() == nil {
			// keep retrying I/O timeout errors since we still have
			// time left
			continue
		}
		// For anything else, don't retry
		break
	}
	if err != nil {
		return
	}

	if protocolId != COMPACT_PROTOCOL_ID {
		e := fmt.Errorf("Expected protocol id %02x but got %02x", COMPACT_PROTOCOL_ID, protocolId)
		return "", typeId, seqId, NewTProtocolExceptionWithType(BAD_VERSION, e)
	}

	versionAndType, err := p.readByteDirect()
	if err != nil {
		return
	}

	version := versionAndType & COMPACT_VERSION_MASK
	typeId = TMessageType((versionAndType >> COMPACT_TYPE_SHIFT_AMOUNT) & COMPACT_TYPE_BITS)
	if version != COMPACT_VERSION {
		e := fmt.Errorf("Expected version %02x but got %02x", COMPACT_VERSION, version)
		err = NewTProtocolExceptionWithType(BAD_VERSION, e)
		return
	}
	seqId, e := p.readVarint32()
	if e != nil {
		err = NewTProtocolException(e)
		return
	}
	name, err = p.ReadString(ctx)
	return
}

func (p *TCompactProtocol) ReadMessageEnd(ctx context.Context) error { return nil }

// Read a struct begin. There's nothing on the wire for this, but it is our
// opportunity to push a new struct begin marker onto the field stack.
func (p *TCompactProtocol) ReadStructBegin(ctx context.Context) (name string, err error) {
	p.lastField = append(p.lastField, p.lastFieldId)
	p.lastFieldId = 0
	return
}

// Doesn't actually consume any wire data, just removes the last field for
// this struct from the field stack.
func (p *TCompactProtocol) ReadStructEnd(ctx context.Context) error {
	// consume the last field we read off the wire.
	if len(p.lastField) <= 0 {
		return NewTProtocolExceptionWithType(INVALID_DATA, errors.New("ReadStructEnd called without matching ReadStructBegin call before"))
	}
	p.lastFieldId = p.lastField[len(p.lastField)-1]
	p.lastField = p.lastField[:len(p.lastField)-1]
	return nil
}

// Read a field header off the wire.
func (p *TCompactProtocol) ReadFieldBegin(ctx context.Context) (name string, typeId TType, id int16, err error) {
	t, err := p.readByteDirect()
	if err != nil {
		return
	}

	// if it's a stop, then we can return immediately, as the struct is over.
	if (t & 0x0f) == STOP {
		return "", STOP, 0, nil
	}

	// mask off the 4 MSB of the type header. it could contain a field id delta.
	modifier := int16((t & 0xf0) >> 4)
	if modifier == 0 {
		// not a delta. look ahead for the zigzag varint field id.
		id, err = p.ReadI16(ctx)
		if err != nil {
			return
		}
	} else {
		// has a delta. add the delta to the last read field id.
		id = int16(p.lastFieldId) + modifier
	}
	typeId, e := p.getTType(tCompactType(t & 0x0f))
	if e != nil {
		err = NewTProtocolException(e)
		return
	}

	// if this happens to be a boolean field, the value is encoded in the type
	if p.isBoolType(t) {
		// save the boolean value in a special instance variable.
		p.boolValue = (byte(t)&0x0f == COMPACT_BOOLEAN_TRUE)
		p.boolValueIsNotNull = true
	}

	// push the new field onto the field stack so we can keep the deltas going.
	p.lastFieldId = int(id)
	return
}

func (p *TCompactProtocol) ReadFieldEnd(ctx context.Context) error { return nil }

// Read a map header off the wire. If the size is zero, skip reading the key
// and value type. This means that 0-length maps will yield TMaps without the
// "correct" types.
func (p *TCompactProtocol) ReadMapBegin(ctx context.Context) (keyType TType, valueType TType, size int, err error) {
	size32, e := p.readVarint32()
	if e != nil {
		err = NewTProtocolException(e)
		return
	}
	size = int(size32)

	keyAndValueType := byte(STOP)
	if size != 0 {
		keyAndValueType, err = p.readByteDirect()
		if err != nil {
			return
		}
	}
	keyType, _ = p.getTType(tCompactType(keyAndValueType >> 4))
	valueType, _ = p.getTType(tCompactType(keyAndValueType & 0xf))

	minElemSize := p.getMinSerializedSize(keyType) + p.getMinSerializedSize(valueType)
	totalMinSize := size32 * minElemSize
	err = checkSizeForProtocol(totalMinSize, p.cfg)
	if err != nil {
		return
	}
	return
}

func (p *TCompactProtocol) ReadMapEnd(ctx context.Context) error { return nil }

// Read a list header off the wire. If the list size is 0-14, the size will
// be packed into the element type header. If it's a longer list, the 4 MSB
// of the element type header will be 0xF, and a varint will follow with the
// true size.
func (p *TCompactProtocol) ReadListBegin(ctx context.Context) (elemType TType, size int, err error) {
	size_and_type, err := p.readByteDirect()
	if err != nil {
		return
	}
	size = int((size_and_type >> 4) & 0x0f)
	if size == 15 {
		size2, e := p.readVarint32()
		if e != nil {
			err = NewTProtocolException(e)
			return
		}
		size = int(size2)
	}
	elemType, e := p.getTType(tCompactType(size_and_type))
	if e != nil {
		err = NewTProtocolException(e)
		return
	}

	minElemSize := p.getMinSerializedSize(elemType)
	totalMinSize := int32(size) * minElemSize
	err = checkSizeForProtocol(totalMinSize, p.cfg)
	if err != nil {
		return
	}
	return
}

func (p *TCompactProtocol) ReadListEnd(ctx context.Context) error { return nil }

// Read a set header off the wire. If the set size is 0-14, the size will
// be packed into the element type header. If it's a longer set, the 4 MSB
// of the element type header will be 0xF, and a varint will follow with the
// true size.
func (p *TCompactProtocol) ReadSetBegin(ctx context.Context) (elemType TType, size int, err error) {
	return p.ReadListBegin(ctx)
}

func (p *TCompactProtocol) ReadSetEnd(ctx context.Context) error { return nil }

// Read a boolean off the wire. If this is a boolean field, the value should
// already have been read during readFieldBegin, so we'll just consume the
// pre-stored value. Otherwise, read a byte.
func (p *TCompactProtocol) ReadBool(ctx context.Context) (value bool, err error) {
	if p.boolValueIsNotNull {
		p.boolValueIsNotNull = false
		return p.boolValue, nil
	}
	v, err := p.readByteDirect()
	return v == COMPACT_BOOLEAN_TRUE, err
}

// Read a single byte off the wire. Nothing interesting here.
func (p *TCompactProtocol) ReadByte(ctx context.Context) (int8, error) {
	v, err := p.readByteDirect()
	if err != nil {
		return 0, NewTProtocolException(err)
	}
	return int8(v), err
}

// Read an i16 from the wire as a zigzag varint.
func (p *TCompactProtocol) ReadI16(ctx context.Context) (value int16, err error) {
	v, err := p.ReadI32(ctx)
	return int16(v), err
}

// Read an i32 from the wire as a zigzag varint.
func (p *TCompactProtocol) ReadI32(ctx context.Context) (value int32, err error) {
	v, e := p.readVarint32()
	if e != nil {
		return 0, NewTProtocolException(e)
	}
	value = p.zigzagToInt32(v)
	return value, nil
}

// Read an i64 from the wire as a zigzag varint.
func (p *TCompactProtocol) ReadI64(ctx context.Context) (value int64, err error) {
	v, e := p.readVarint64()
	if e != nil {
		return 0, NewTProtocolException(e)
	}
	value = p.zigzagToInt64(v)
	return value, nil
}

// No magic here - just read a double off the wire.
func (p *TCompactProtocol) ReadDouble(ctx context.Context) (value float64, err error) {
	longBits := p.buffer[0:8]
	_, e := io.ReadFull(p.trans, longBits)
	if e != nil {
		return 0.0, NewTProtocolException(e)
	}
	return math.Float64frombits(p.bytesToUint64(longBits)), nil
}

// Reads a []byte (via readBinary), and then UTF-8 decodes it.
func (p *TCompactProtocol) ReadString(ctx context.Context) (value string, err error) {
	length, e := p.readVarint32()
	if e != nil {
		return "", NewTProtocolException(e)
	}
	err = checkSizeForProtocol(length, p.cfg)
	if err != nil {
		return
	}
	if length == 0 {
		return "", nil
	}
	if length < int32(len(p.buffer)) {
		// Avoid allocation on small reads
		buf := p.buffer[:length]
		read, e := io.ReadFull(p.trans, buf)
		return string(buf[:read]), NewTProtocolException(e)
	}

	buf, e := safeReadBytes(length, p.trans)
	return string(buf), NewTProtocolException(e)
}

// Read a []byte from the wire.
func (p *TCompactProtocol) ReadBinary(ctx context.Context) (value []byte, err error) {
	length, e := p.readVarint32()
	if e != nil {
		return nil, NewTProtocolException(e)
	}
	err = checkSizeForProtocol(length, p.cfg)
	if err != nil {
		return
	}
	if length == 0 {
		return []byte{}, nil
	}

	buf, e := safeReadBytes(length, p.trans)
	return buf, NewTProtocolException(e)
}

// Read fixed 16 bytes as UUID.
func (p *TCompactProtocol) ReadUUID(ctx context.Context) (value Tuuid, err error) {
	buf := p.buffer[0:16]
	_, e := io.ReadFull(p.trans, buf)
	if e == nil {
		copy(value[:], buf)
	}
	return value, NewTProtocolException(e)
}

func (p *TCompactProtocol) Flush(ctx context.Context) (err error) {
	return NewTProtocolException(p.trans.Flush(ctx))
}

func (p *TCompactProtocol) Skip(ctx context.Context, fieldType TType) (err error) {
	return SkipDefaultDepth(ctx, p, fieldType)
}

func (p *TCompactProtocol) Transport() TTransport {
	return p.origTransport
}

//
// Internal writing methods
//

// Abstract method for writing the start of lists and sets. List and sets on
// the wire differ only by the type indicator.
func (p *TCompactProtocol) writeCollectionBegin(elemType TType, size int) (int, error) {
	if size <= 14 {
		return 1, p.writeByteDirect(byte(int32(size<<4) | int32(p.getCompactType(elemType))))
	}
	err := p.writeByteDirect(0xf0 | byte(p.getCompactType(elemType)))
	if err != nil {
		return 0, err
	}
	m, err := p.writeVarint32(int32(size))
	return 1 + m, err
}

// Write an i32 as a varint. Results in 1-5 bytes on the wire.
// TODO(pomack): make a permanent buffer like writeVarint64?
func (p *TCompactProtocol) writeVarint32(n int32) (int, error) {
	i32buf := p.buffer[0:5]
	idx := 0
	for {
		if (n & ^0x7F) == 0 {
			i32buf[idx] = byte(n)
			idx++
			// p.writeByteDirect(byte(n));
			break
			// return;
		} else {
			i32buf[idx] = byte((n & 0x7F) | 0x80)
			idx++
			// p.writeByteDirect(byte(((n & 0x7F) | 0x80)));
			u := uint32(n)
			n = int32(u >> 7)
		}
	}
	return p.trans.Write(i32buf[0:idx])
}

// Write an i64 as a varint. Results in 1-10 bytes on the wire.
func (p *TCompactProtocol) writeVarint64(n int64) (int, error) {
	varint64out := p.buffer[0:10]
	idx := 0
	for {
		if (n & ^0x7F) == 0 {
			varint64out[idx] = byte(n)
			idx++
			break
		} else {
			varint64out[idx] = byte((n & 0x7F) | 0x80)
			idx++
			u := uint64(n)
			n = int64(u >> 7)
		}
	}
	return p.trans.Write(varint64out[0:idx])
}

// Convert l into a zigzag long. This allows negative numbers to be
// represented compactly as a varint.
func (p *TCompactProtocol) int64ToZigzag(l int64) int64 {
	return (l << 1) ^ (l >> 63)
}

// Convert l into a zigzag long. This allows negative numbers to be
// represented compactly as a varint.
func (p *TCompactProtocol) int32ToZigzag(n int32) int32 {
	return (n << 1) ^ (n >> 31)
}

// Writes a byte without any possibility of all that field header nonsense.
// Used internally by other writing methods that know they need to write a byte.
func (p *TCompactProtocol) writeByteDirect(b byte) error {
	return p.trans.WriteByte(b)
}

//
// Internal reading methods
//

// Read an i32 from the wire as a varint. The MSB of each byte is set
// if there is another byte to follow. This can read up to 5 bytes.
func (p *TCompactProtocol) readVarint32() (int32, error) {
	// if the wire contains the right stuff, this will just truncate the i64 we
	// read and get us the right sign.
	v, err := p.readVarint64()
	return int32(v), err
}

// Read an i64 from the wire as a proper varint. The MSB of each byte is set
// if there is another byte to follow. This can read up to 10 bytes.
func (p *TCompactProtocol) readVarint64() (int64, error) {
	shift := uint(0)
	result := int64(0)
	for {
		b, err := p.readByteDirect()
		if err != nil {
			return 0, err
		}
		result |= int64(b&0x7f) << shift
		if (b & 0x80) != 0x80 {
			break
		}
		shift += 7
	}
	return result, nil
}

// Read a byte, unlike ReadByte that reads Thrift-byte that is i8.
func (p *TCompactProtocol) readByteDirect() (byte, error) {
	return p.trans.ReadByte()
}

//
// encoding helpers
//

// Convert from zigzag int to int.
func (p *TCompactProtocol) zigzagToInt32(n int32) int32 {
	u := uint32(n)
	return int32(u>>1) ^ -(n & 1)
}

// Convert from zigzag long to long.
func (p *TCompactProtocol) zigzagToInt64(n int64) int64 {
	u := uint64(n)
	return int64(u>>1) ^ -(n & 1)
}

// Note that it's important that the mask bytes are long literals,
// otherwise they'll default to ints, and when you shift an int left 56 bits,
// you just get a messed up int.
func (p *TCompactProtocol) bytesToUint64(b []byte) uint64 {
	return binary.LittleEndian.Uint64(b)
}

//
// type testing and converting
//

func (p *TCompactProtocol) isBoolType(b byte) bool {
	return (b&0x0f) == COMPACT_BOOLEAN_TRUE || (b&0x0f) == COMPACT_BOOLEAN_FALSE
}

// Given a tCompactType constant, convert it to its corresponding
// TType value.
func (p *TCompactProtocol) getTType(t tCompactType) (TType, error) {
	switch byte(t) & 0x0f {
	case STOP:
		return STOP, nil
	case COMPACT_BOOLEAN_FALSE, COMPACT_BOOLEAN_TRUE:
		return BOOL, nil
	case COMPACT_BYTE:
		return BYTE, nil
	case COMPACT_I16:
		return I16, nil
	case COMPACT_I32:
		return I32, nil
	case COMPACT_I64:
		return I64, nil
	case COMPACT_DOUBLE:
		return DOUBLE, nil
	case COMPACT_BINARY:
		return STRING, nil
	case COMPACT_LIST:
		return LIST, nil
	case COMPACT_SET:
		return SET, nil
	case COMPACT_MAP:
		return MAP, nil
	case COMPACT_STRUCT:
		return STRUCT, nil
	case COMPACT_UUID:
		return UUID, nil
	}
	return STOP, NewTProtocolException(fmt.Errorf("don't know what type: %v", t&0x0f))
}

// Given a TType value, find the appropriate TCompactProtocol.Types constant.
func (p *TCompactProtocol) getCompactType(t TType) tCompactType {
	return ttypeToCompactType[t]
}

func (p *TCompactProtocol) SetTConfiguration(conf *TConfiguration) {
	PropagateTConfiguration(p.trans, conf)
	PropagateTConfiguration(p.origTransport, conf)
	p.cfg = conf
}

// Return the minimum number of bytes a type will consume on the wire
func (p *TCompactProtocol) getMinSerializedSize(ttype TType) int32 {
	switch ttype {
	case STOP:
		return 1 // T_STOP needs to count itself
	case VOID:
		return 1 // T_VOID needs to count itself
	case BOOL:
		return 1 // sizeof(int8)
	case BYTE:
		return 1 // sizeof(int8)
	case DOUBLE:
		return 8 // uses PutUint64() which always writes 8 bytes
	case I16:
		return 1 // zigzag
	case I32:
		return 1 // zigzag
	case I64:
		return 1 // zigzag
	case STRING:
		return 1 // string length
	case STRUCT:
		return 1 // empty struct needs at least 1 byte for the T_STOP
	case MAP:
		return 1 // element count
	case SET:
		return 1 // element count
	case LIST:
		return 1 // element count
	case UUID:
		return 16 // 16 bytes
	default:
		return 1 // unknown type
	}
}

var (
	_ TConfigurationSetter = (*TCompactProtocolFactory)(nil)
	_ TConfigurationSetter = (*TCompactProtocol)(nil)
)
