unit TestSerializer.Tests;
(*
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
 *)

interface

uses
  Classes,
  Windows,
  SysUtils,
  Generics.Collections,
  Thrift,
  Thrift.Exception,
  Thrift.Socket,
  Thrift.Transport,
  Thrift.Protocol,
  Thrift.Protocol.JSON,
  Thrift.Protocol.Compact,
  Thrift.Collections,
  Thrift.Configuration,
  Thrift.Server,
  Thrift.Utils,
  Thrift.Serializer,
  Thrift.Stream,
  Thrift.WinHTTP,
  Thrift.TypeRegistry,
  System_,
  test.ExceptionStruct,
  test.SimpleException,
  DebugProtoTest;

{$TYPEINFO ON}

type
  TFactoryPair = record
    proto : IProtocolFactory;
    trans : ITransportFactory;
  end;

  TTestSerializer = class //extends TestCase {
  private type
    TMethod = (
      mt_Bytes,
      mt_Stream
    );

  strict private
    FProtocols : TList< TFactoryPair>;
    procedure AddFactoryCombination( const aProto : IProtocolFactory; const aTrans : ITransportFactory);
    class function UserFriendlyName( const factory : TFactoryPair) : string;  overload;
    class function UserFriendlyName( const method : TMethod) : string;  overload;

    class function  Serialize(const input : IBase; const factory : TFactoryPair) : TBytes;  overload;
    class procedure Serialize(const input : IBase; const factory : TFactoryPair; const aStream : TStream);  overload;

    class procedure Deserialize( const input : TBytes; const target : IBase; const factory : TFactoryPair);  overload;
    class procedure Deserialize( const input : TStream; const target : IBase; const factory : TFactoryPair);  overload;

    class procedure Deserialize( const input : TBytes; out target : TGuid; const factory : TFactoryPair);  overload;

    class procedure ValidateReadToEnd( const serial : TDeserializer);  overload;

    class function LengthOf( const bytes : TBytes) : Integer; overload; inline;
    class function LengthOf( const bytes : IThriftBytes) : Integer; overload; inline;

    class function DataPtrOf( const bytes : TBytes) : Pointer; overload; inline;
    class function DataPtrOf( const bytes : IThriftBytes) : Pointer; overload; inline;

    procedure Test_Serializer_Deserializer;
    procedure Test_COM_Types;
    procedure Test_ThriftBytesCTORs;

    procedure Test_OneOfEach(       const method : TMethod; const factory : TFactoryPair; const stream : TFileStream);
    procedure Test_CompactStruct(   const method : TMethod; const factory : TFactoryPair; const stream : TFileStream);
    procedure Test_ExceptionStruct( const method : TMethod; const factory : TFactoryPair; const stream : TFileStream);
    procedure Test_SimpleException( const method : TMethod; const factory : TFactoryPair; const stream : TFileStream);

    procedure Test_ProtocolConformity( const factory : TFactoryPair; const stream : TFileStream);
    procedure Test_UuidDeserialization( const factory : TFactoryPair; const stream : TFileStream);

  public
    constructor Create;
    destructor Destroy;  override;

    procedure RunTests;
  end;


implementation

const SERIALIZERDATA_DLL = 'SerializerData.dll';
function CreateOneOfEach : IOneOfEach; stdcall; external SERIALIZERDATA_DLL;
function CreateNesting : INesting; stdcall; external SERIALIZERDATA_DLL;
function CreateHolyMoley : IHolyMoley; stdcall; external SERIALIZERDATA_DLL;
function CreateCompactProtoTestStruct : ICompactProtoTestStruct; stdcall; external SERIALIZERDATA_DLL;
function CreateBatchGetResponse : IBatchGetResponse; stdcall; external SERIALIZERDATA_DLL;
function CreateSimpleException : IError; stdcall; external SERIALIZERDATA_DLL;


{ TTestSerializer }

constructor TTestSerializer.Create;
begin
  inherited Create;
  FProtocols := TList< TFactoryPair>.Create;

  AddFactoryCombination( TBinaryProtocolImpl.TFactory.Create, nil);
  AddFactoryCombination( TCompactProtocolImpl.TFactory.Create, nil);
  AddFactoryCombination( TJSONProtocolImpl.TFactory.Create, nil);

  AddFactoryCombination( TBinaryProtocolImpl.TFactory.Create, TFramedTransportImpl.TFactory.Create);
  AddFactoryCombination( TCompactProtocolImpl.TFactory.Create, TFramedTransportImpl.TFactory.Create);
  AddFactoryCombination( TJSONProtocolImpl.TFactory.Create, TFramedTransportImpl.TFactory.Create);

  AddFactoryCombination( TBinaryProtocolImpl.TFactory.Create, TBufferedTransportImpl.TFactory.Create);
  AddFactoryCombination( TCompactProtocolImpl.TFactory.Create, TBufferedTransportImpl.TFactory.Create);
  AddFactoryCombination( TJSONProtocolImpl.TFactory.Create, TBufferedTransportImpl.TFactory.Create);
end;


destructor TTestSerializer.Destroy;
begin
  try
    FreeAndNil( FProtocols);
  finally
    inherited Destroy;
  end;
end;


procedure TTestSerializer.AddFactoryCombination( const aProto : IProtocolFactory; const aTrans : ITransportFactory);
var rec : TFactoryPair;
begin
  rec.proto := aProto;
  rec.trans := aTrans;
  FProtocols.Add( rec);
end;


class function TTestSerializer.LengthOf( const bytes : TBytes) : Integer;
begin
  result := Length(bytes);
end;


class function TTestSerializer.LengthOf( const bytes : IThriftBytes) : Integer;
begin
  if bytes <> nil
  then result := bytes.Count
  else result := 0;
end;


class function TTestSerializer.DataPtrOf( const bytes : TBytes) : Pointer;
begin
  result := bytes;
end;


class function TTestSerializer.DataPtrOf( const bytes : IThriftBytes) : Pointer;
begin
  if bytes <> nil
  then result := bytes.QueryRawDataPtr
  else result := nil;
end;


procedure TTestSerializer.Test_ProtocolConformity( const factory : TFactoryPair; const stream : TFileStream);
begin
  Test_UuidDeserialization( factory, stream);
  // add more tests here
end;


procedure TTestSerializer.Test_UuidDeserialization( const factory : TFactoryPair; const stream : TFileStream);

  function CreateGuidBytes : TBytes;
  var obj : TObject;
      i : Integer;
  begin
    obj := factory.proto as TObject;

    if obj is TJSONProtocolImpl.TFactory then begin
      result := TEncoding.UTF8.GetBytes('"00112233-4455-6677-8899-aabbccddeeff"');
      Exit;
    end;

    if (obj is TBinaryProtocolImpl.TFactory)
    or (obj is TCompactProtocolImpl.TFactory)
    then begin
      SetLength(result,16);
      for i := 0 to Length(result)-1 do result[i] := (i * $10) + i;
      Exit;
    end;

    raise Exception.Create('Unhandled case');
  end;


var tested, correct : TGuid;
    bytes   : TBytes;
begin
  // write
  bytes := CreateGuidBytes();

  // init + read
  Deserialize( bytes, tested, factory);

  // check
  correct := TGuid.Create('{00112233-4455-6677-8899-aabbccddeeff}');
  ASSERT( tested = correct);
end;


procedure TTestSerializer.Test_OneOfEach( const method : TMethod; const factory : TFactoryPair; const stream : TFileStream);
var tested, correct : IOneOfEach;
    bytes   : TBytes;
    i : Integer;
begin
  // write
  tested := CreateOneOfEach;
  case method of
    mt_Bytes:  bytes := Serialize( tested, factory);
    mt_Stream: begin
      stream.Size := 0;
      Serialize( tested, factory, stream);
    end
  else
    ASSERT( FALSE);
  end;

  // init + read
  tested := TOneOfEachImpl.Create;
  case method of
    mt_Bytes:  Deserialize( bytes, tested, factory);
    mt_Stream: begin
      stream.Position := 0;
      Deserialize( stream, tested, factory);
    end
  else
    ASSERT( FALSE);
  end;

  // check
  correct := CreateOneOfEach;
  ASSERT( tested.Im_true = correct.Im_true);
  ASSERT( tested.Im_false = correct.Im_false);
  ASSERT( tested.A_bite = correct.A_bite);
  ASSERT( tested.Integer16 = correct.Integer16);
  ASSERT( tested.Integer32 = correct.Integer32);
  ASSERT( tested.Integer64 = correct.Integer64);
  ASSERT( Abs( tested.Double_precision - correct.Double_precision) < 1E-12);
  ASSERT( tested.Some_characters = correct.Some_characters);
  ASSERT( tested.Zomg_unicode = correct.Zomg_unicode);
  ASSERT( tested.Rfc4122_uuid = correct.Rfc4122_uuid);
  ASSERT( tested.What_who = correct.What_who);

  ASSERT( LengthOf(tested.Base64) = LengthOf(correct.Base64));
  ASSERT( CompareMem( DataPtrOf(tested.Base64), DataPtrOf(correct.Base64), LengthOf(correct.Base64)));

  ASSERT( tested.Byte_list.Count = correct.Byte_list.Count);
  for i := 0 to tested.Byte_list.Count-1
  do ASSERT( tested.Byte_list[i] = correct.Byte_list[i]);

  ASSERT( tested.I16_list.Count = correct.I16_list.Count);
  for i := 0 to tested.I16_list.Count-1
  do ASSERT( tested.I16_list[i] = correct.I16_list[i]);

  ASSERT( tested.I64_list.Count = correct.I64_list.Count);
  for i := 0 to tested.I64_list.Count-1
  do ASSERT( tested.I64_list[i] = correct.I64_list[i]);
end;


procedure TTestSerializer.Test_CompactStruct( const method : TMethod; const factory : TFactoryPair; const stream : TFileStream);
var tested, correct : ICompactProtoTestStruct;
    bytes   : TBytes;
begin
  // write
  tested := CreateCompactProtoTestStruct;
  case method of
    mt_Bytes:  bytes := Serialize( tested, factory);
    mt_Stream: begin
      stream.Size := 0;
      Serialize( tested, factory, stream);
    end
  else
    ASSERT( FALSE);
  end;

  // init + read
  correct := TCompactProtoTestStructImpl.Create;
  case method of
    mt_Bytes:  Deserialize( bytes, tested, factory);
    mt_Stream: begin
      stream.Position := 0;
      Deserialize( stream, tested, factory);
    end
  else
    ASSERT( FALSE);
  end;

  // check
  correct := CreateCompactProtoTestStruct;
  ASSERT( correct.Field500  = tested.Field500);
  ASSERT( correct.Field5000  = tested.Field5000);
  ASSERT( correct.Field20000 = tested.Field20000);
end;


procedure TTestSerializer.Test_ExceptionStruct( const method : TMethod; const factory : TFactoryPair; const stream : TFileStream);
var tested, correct : IBatchGetResponse;
    bytes   : TBytes;
    corrDP, testDP : TPair<WideString, IGetRequest>;
    corrEP, testEP : TPair<WideString, ISomeException>;
begin
  // write
  tested := CreateBatchGetResponse;
  case method of
    mt_Bytes:  bytes := Serialize( tested, factory);
    mt_Stream: begin
      stream.Size := 0;
      Serialize( tested, factory, stream);
    end
  else
    ASSERT( FALSE);
  end;

  // init + read
  correct := TBatchGetResponseImpl.Create;
  case method of
    mt_Bytes:  Deserialize( bytes, tested, factory);
    mt_Stream: begin
      stream.Position := 0;
      Deserialize( stream, tested, factory);
    end
  else
    ASSERT( FALSE);
  end;

  // check
  correct := CreateBatchGetResponse;

  // rewrite the following test if not
  ASSERT( tested.Responses.Count = 1);
  ASSERT( correct.Responses.Count = tested.Responses.Count);
  for corrDP in correct.Responses do begin
    for testDP in tested.Responses do begin
      ASSERT( corrDP.Key = testDP.Key);
      ASSERT( corrDP.Value.Id = testDP.Value.Id);
      ASSERT( corrDP.Value.Data.Count = testDP.Value.Data.Count);
    end;
  end;

  // rewrite the following test if not
  ASSERT( tested.Errors.Count = 1);
  ASSERT( correct.Errors.Count = tested.Errors.Count);
  for corrEP in correct.Errors do begin
    for testEP in tested.Errors do begin
      ASSERT( corrEP.Key = testEP.Key);
      ASSERT( corrEP.Value.Error = testEP.Value.Error);
    end;
  end;
end;


procedure TTestSerializer.Test_SimpleException( const method : TMethod; const factory : TFactoryPair; const stream : TFileStream);
var tested, correct : IError;
    bytes   : TBytes;
begin
  // write
  tested := CreateSimpleException;
  case method of
    mt_Bytes:  bytes := Serialize( tested, factory);
    mt_Stream: begin
      stream.Size := 0;
      Serialize( tested, factory, stream);
    end
  else
    ASSERT( FALSE);
  end;

  // init + read
  correct := TErrorImpl.Create;
  case method of
    mt_Bytes:  Deserialize( bytes, tested, factory);
    mt_Stream: begin
      stream.Position := 0;
      Deserialize( stream, tested, factory);
    end
  else
    ASSERT( FALSE);
  end;

  // check
  correct := CreateSimpleException;
  while correct <> nil do begin
    // validate
    ASSERT( correct.ErrorCode = tested.ErrorCode);
    ASSERT( IsEqualGUID( correct.ExceptionData, tested.ExceptionData));

    // iterate
    correct := correct.InnerException;
    tested  := tested.InnerException;
    ASSERT( (tested <> nil) xor (correct = nil));  // both or none
  end;
end;


procedure TTestSerializer.Test_Serializer_Deserializer;
var factory : TFactoryPair;
    stream  : TFileStream;
    method  : TMethod;
begin
  stream  := TFileStream.Create( 'TestSerializer.dat', fmCreate);
  try
    for method in [Low(TMethod)..High(TMethod)] do begin
      Writeln( UserFriendlyName(method));

      for factory in FProtocols do begin
        Writeln('- '+UserFriendlyName(factory));

        // protocol conformity tests
        if (method = TMethod.mt_Bytes) and (factory.trans = nil)
        then Test_ProtocolConformity( factory, stream);

        // normal objects
        Test_OneOfEach(       method, factory, stream);
        Test_CompactStruct(   method, factory, stream);
        Test_ExceptionStruct( method, factory, stream);
        Test_SimpleException( method, factory, stream);
      end;

      Writeln;
    end;

  finally
    stream.Free;
  end;
end;


class function TTestSerializer.UserFriendlyName( const factory : TFactoryPair) : string;
begin
  result := Copy( (factory.proto as TObject).ClassName, 2, MAXINT);

  if factory.trans <> nil
  then result := Copy( (factory.trans as TObject).ClassName, 2, MAXINT) +' '+ result;

  result := StringReplace( result, 'Impl', '', [rfReplaceAll]);
  result := StringReplace( result, 'Transport.TFactory', '', [rfReplaceAll]);
  result := StringReplace( result, 'Protocol.TFactory', '', [rfReplaceAll]);
end;


class function TTestSerializer.UserFriendlyName( const method : TMethod) : string;
const NAMES : array[TMethod] of string = ('TBytes','Stream');
begin
  result := NAMES[method];
end;


procedure TTestSerializer.Test_COM_Types;
var tested : IOneOfEach;
begin
  {$IF cDebugProtoTest_Option_COM_types}
  ASSERT( SizeOf(TSomeEnum) = SizeOf(Int32));  // -> MINENUMSIZE 4

  // try to set values that allocate memory
  tested := CreateOneOfEach;
  tested.Zomg_unicode := 'This is a test';
  tested.Base64 := TThriftBytesImpl.Create( TEncoding.UTF8.GetBytes('abc'));
  {$IFEND}
end;


procedure TTestSerializer.Test_ThriftBytesCTORs;
var one, two : IThriftBytes;
    bytes : TBytes;
    sAscii : AnsiString;
begin
  sAscii := 'ABC/xzy';
  bytes  := TEncoding.ASCII.GetBytes(string(sAscii));

  one := TThriftBytesImpl.Create( PAnsiChar(sAscii), Length(sAscii));
  two := TThriftBytesImpl.Create( bytes, TRUE);

  ASSERT( one.Count = two.Count);
  ASSERT( CompareMem( one.QueryRawDataPtr, two.QueryRawDataPtr, one.Count));
end;


procedure TTestSerializer.RunTests;
begin
  try
    Test_Serializer_Deserializer;
    Test_COM_Types;
    Test_ThriftBytesCTORs;
  except
    on e:Exception do begin
      Writeln( e.ClassName+': '+ e.Message);
      Write('Hit ENTER to close ... '); Readln;
    end;
  end;
end;


class function TTestSerializer.Serialize(const input : IBase; const factory : TFactoryPair) : TBytes;
var serial : TSerializer;
    config : IThriftConfiguration;
begin
  config := TThriftConfigurationImpl.Create;
  //config.MaxMessageSize := 0;   // we don't read anything here

  serial := TSerializer.Create( factory.proto, factory.trans, config);
  try
    result := serial.Serialize( input);
  finally
    serial.Free;
  end;
end;


class procedure TTestSerializer.Serialize(const input : IBase; const factory : TFactoryPair; const aStream : TStream);
var serial : TSerializer;
    config : IThriftConfiguration;
begin
  config := TThriftConfigurationImpl.Create;
  //config.MaxMessageSize := 0;   // we don't read anything here

  serial := TSerializer.Create( factory.proto, factory.trans, config);
  try
    serial.Serialize( input, aStream);
  finally
    serial.Free;
  end;
end;


class procedure TTestSerializer.Deserialize( const input : TBytes; const target : IBase; const factory : TFactoryPair);
var serial : TDeserializer;
    config : IThriftConfiguration;
begin
  config := TThriftConfigurationImpl.Create;
  config.MaxMessageSize := Length(input);

  serial := TDeserializer.Create( factory.proto, factory.trans, config);
  try
    serial.Deserialize( input, target);
    ValidateReadToEnd( serial);
  finally
    serial.Free;
  end;
end;


class procedure TTestSerializer.Deserialize( const input : TStream; const target : IBase; const factory : TFactoryPair);
var serial : TDeserializer;
    config : IThriftConfiguration;
begin
  config := TThriftConfigurationImpl.Create;
  config.MaxMessageSize := input.Size;

  serial := TDeserializer.Create( factory.proto, factory.trans, config);
  try
    serial.Deserialize( input, target);
    ValidateReadToEnd( serial);
  finally
    serial.Free;
  end;
end;


class procedure TTestSerializer.Deserialize( const input : TBytes; out target : TGuid; const factory : TFactoryPair);
var serial : TDeserializer;
    config : IThriftConfiguration;
begin
  config := TThriftConfigurationImpl.Create;
  config.MaxMessageSize := Length(input);

  serial := TDeserializer.Create( factory.proto, factory.trans, config);
  try
    serial.Stream.Write(input[0], Length(input));
    serial.Stream.Position := 0;
    serial.Transport.ResetMessageSizeAndConsumedBytes();  // size has changed

    target := serial.Protocol.ReadUuid;
  finally
    serial.Free;
  end;
end;


class procedure TTestSerializer.ValidateReadToEnd( const serial : TDeserializer);
// we should not have any more byte to read
var dummy : IBase;
begin
  try
    dummy := TOneOfEachImpl.Create;
    serial.Deserialize( nil, dummy);
    raise EInOutError.Create('Expected exception not thrown?');
  except
    on e:TTransportException do {expected};
    on e:Exception do raise; // unexpected
  end;
end;


end.
