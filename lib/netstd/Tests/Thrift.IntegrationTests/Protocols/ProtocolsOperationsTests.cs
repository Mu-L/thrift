// Licensed to the Apache Software Foundation(ASF) under one
// or more contributor license agreements.See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License. You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the License for the
// specific language governing permissions and limitations
// under the License.

using System;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using KellermanSoftware.CompareNetObjects;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Thrift.Protocol;
using Thrift.Protocol.Entities;
using Thrift.Transport;
using Thrift.Transport.Client;

#pragma warning disable IDE0063  // using

namespace Thrift.IntegrationTests.Protocols
{
    [TestClass]
    public class ProtocolsOperationsTests : TestBase
    {
        [DataTestMethod]
        [DataRow(typeof(TBinaryProtocol), TMessageType.Call)]
        [DataRow(typeof(TBinaryProtocol), TMessageType.Exception)]
        [DataRow(typeof(TBinaryProtocol), TMessageType.Oneway)]
        [DataRow(typeof(TBinaryProtocol), TMessageType.Reply)]
        [DataRow(typeof(TCompactProtocol), TMessageType.Call)]
        [DataRow(typeof(TCompactProtocol), TMessageType.Exception)]
        [DataRow(typeof(TCompactProtocol), TMessageType.Oneway)]
        [DataRow(typeof(TCompactProtocol), TMessageType.Reply)]
        [DataRow(typeof(TJsonProtocol), TMessageType.Call)]
        [DataRow(typeof(TJsonProtocol), TMessageType.Exception)]
        [DataRow(typeof(TJsonProtocol), TMessageType.Oneway)]
        [DataRow(typeof(TJsonProtocol), TMessageType.Reply)]
        public async Task WriteReadMessage_Test(Type protocolType, TMessageType messageType)
        {
            var expected = new TMessage(nameof(TMessage), messageType, 1);

            try
            {
                var tuple = GetProtocolInstance(protocolType);
                using (var stream = tuple.Stream)
                {
                    var protocol = tuple.Protocol;

                    await protocol.WriteMessageBeginAsync(expected, default);
                    await protocol.WriteMessageEndAsync(default);
                    await tuple.Transport.FlushAsync(default);

                    stream.Seek(0, SeekOrigin.Begin);

                    var actualMessage = await protocol.ReadMessageBeginAsync(default);
                    await protocol.ReadMessageEndAsync(default);

                    var result = _compareLogic.Compare(expected, actualMessage);
                    Assert.IsTrue(result.AreEqual, result.DifferencesString);
                }
            }
            catch (Exception e)
            {
                throw new Exception($"Exception during testing of protocol: {protocolType.FullName}", e);
            }
        }

        [DataTestMethod]
        [DataRow(typeof(TBinaryProtocol))]
        [DataRow(typeof(TCompactProtocol))]
        [DataRow(typeof(TJsonProtocol))]
        [ExpectedException(typeof(Exception))]
        public async Task WriteReadStruct_Test(Type protocolType)
        {
            var expected = new TStruct(nameof(TStruct));

            try
            {
                var tuple = GetProtocolInstance(protocolType);
                using (var stream = tuple.Stream)
                {
                    var protocol = tuple.Protocol;

                    await protocol.WriteStructBeginAsync(expected, default);
                    await protocol.WriteStructEndAsync(default);
                    await tuple.Transport.FlushAsync(default);

                    stream.Seek(0, SeekOrigin.Begin);

                    var actual = await protocol.ReadStructBeginAsync(default);
                    await protocol.ReadStructEndAsync(default);

                    var result = _compareLogic.Compare(expected, actual);
                    Assert.IsTrue(result.AreEqual, result.DifferencesString);
                }

            }
            catch (Exception e)
            {
                throw new Exception($"Exception during testing of protocol: {protocolType.FullName}", e);
            }
        }

        [DataTestMethod]
        [DataRow(typeof(TBinaryProtocol))]
        [DataRow(typeof(TCompactProtocol))]
        [DataRow(typeof(TJsonProtocol))]
        [ExpectedException(typeof(Exception))]
        public async Task WriteReadField_Test(Type protocolType)
        {
            var expected = new TField(nameof(TField), TType.String, 1);

            try
            {
                var tuple = GetProtocolInstance(protocolType);
                using (var stream = tuple.Stream)
                {
                    var protocol = tuple.Protocol;

                    await protocol.WriteFieldBeginAsync(expected, default);
                    await protocol.WriteFieldEndAsync(default);
                    await tuple.Transport.FlushAsync(default);

                    stream.Seek(0, SeekOrigin.Begin);

                    var actual = await protocol.ReadFieldBeginAsync(default);
                    await protocol.ReadFieldEndAsync(default);

                    var result = _compareLogic.Compare(expected, actual);
                    Assert.IsTrue(result.AreEqual, result.DifferencesString);
                }
            }
            catch (Exception e)
            {
                throw new Exception($"Exception during testing of protocol: {protocolType.FullName}", e);
            }
        }

        [DataTestMethod]
        [DataRow(typeof(TBinaryProtocol))]
        [DataRow(typeof(TCompactProtocol))]
        [DataRow(typeof(TJsonProtocol))]
        public async Task WriteReadMap_Test(Type protocolType)
        {
            var expected = new TMap(TType.String, TType.String, 1);

            try
            {
                var tuple = GetProtocolInstance(protocolType);
                using (var stream = tuple.Stream)
                {
                    var protocol = tuple.Protocol;

                    await protocol.WriteMapBeginAsync(expected, default);
                    await protocol.WriteMapEndAsync(default);
                    await tuple.Transport.FlushAsync(default);

                    stream.Seek(0, SeekOrigin.Begin);

                    var actual = await protocol.ReadMapBeginAsync(default);
                    await protocol.ReadMapEndAsync(default);

                    var result = _compareLogic.Compare(expected, actual);
                    Assert.IsTrue(result.AreEqual, result.DifferencesString);
                }
            }
            catch (Exception e)
            {
                throw new Exception($"Exception during testing of protocol: {protocolType.FullName}", e);
            }

        }

        [DataTestMethod]
        [DataRow(typeof(TBinaryProtocol))]
        [DataRow(typeof(TCompactProtocol))]
        [DataRow(typeof(TJsonProtocol))]
        public async Task WriteReadList_Test(Type protocolType)
        {
            var expected = new TList(TType.String, 1);

            try
            {
                var tuple = GetProtocolInstance(protocolType);
                using (var stream = tuple.Stream)
                {
                    var protocol = tuple.Protocol;

                    await protocol.WriteListBeginAsync(expected, default);
                    await protocol.WriteListEndAsync(default);
                    await tuple.Transport.FlushAsync(default);

                    stream.Seek(0, SeekOrigin.Begin);

                    var actual = await protocol.ReadListBeginAsync(default);
                    await protocol.ReadListEndAsync(default);

                    var result = _compareLogic.Compare(expected, actual);
                    Assert.IsTrue(result.AreEqual, result.DifferencesString);
                }
            }
            catch (Exception e)
            {
                throw new Exception($"Exception during testing of protocol: {protocolType.FullName}", e);
            }
        }

        [DataTestMethod]
        [DataRow(typeof(TBinaryProtocol))]
        [DataRow(typeof(TCompactProtocol))]
        [DataRow(typeof(TJsonProtocol))]
        public async Task WriteReadSet_Test(Type protocolType)
        {
            var expected = new TSet(TType.String, 1);

            try
            {
                var tuple = GetProtocolInstance(protocolType);
                using (var stream = tuple.Stream)
                {
                    var protocol = tuple.Protocol;

                    await protocol.WriteSetBeginAsync(expected, default);
                    await protocol.WriteSetEndAsync(default);
                    await tuple.Transport.FlushAsync(default);

                    stream.Seek(0, SeekOrigin.Begin);

                    var actual = await protocol.ReadSetBeginAsync(default);
                    await protocol.ReadSetEndAsync(default);

                    var result = _compareLogic.Compare(expected, actual);
                    Assert.IsTrue(result.AreEqual, result.DifferencesString);
                }
            }
            catch (Exception e)
            {
                throw new Exception($"Exception during testing of protocol: {protocolType.FullName}", e);
            }
        }

        [DataTestMethod]
        [DataRow(typeof(TBinaryProtocol))]
        [DataRow(typeof(TCompactProtocol))]
        [DataRow(typeof(TJsonProtocol))]
        public async Task WriteReadBool_Test(Type protocolType)
        {
            var expected = true;

            try
            {
                var tuple = GetProtocolInstance(protocolType);
                using (var stream = tuple.Stream)
                {
                    var protocol = tuple.Protocol;

                    await protocol.WriteBoolAsync(expected, default);
                    await tuple.Transport.FlushAsync(default);

                    stream.Seek(0, SeekOrigin.Begin);

                    var actual = await protocol.ReadBoolAsync(default);

                    var result = _compareLogic.Compare(expected, actual);
                    Assert.IsTrue(result.AreEqual, result.DifferencesString);
                }
            }
            catch (Exception e)
            {
                throw new Exception($"Exception during testing of protocol: {protocolType.FullName}", e);
            }
        }

        [DataTestMethod]
        [DataRow(typeof(TBinaryProtocol))]
        [DataRow(typeof(TCompactProtocol))]
        [DataRow(typeof(TJsonProtocol))]
        public async Task WriteReadByte_Test(Type protocolType)
        {
            var expected = sbyte.MaxValue;

            try
            {
                var tuple = GetProtocolInstance(protocolType);
                using (var stream = tuple.Stream)
                {
                    var protocol = tuple.Protocol;

                    await protocol.WriteByteAsync(expected, default);
                    await tuple.Transport.FlushAsync(default);

                    stream.Seek(0, SeekOrigin.Begin);

                    var actual = await protocol.ReadByteAsync(default);

                    var result = _compareLogic.Compare(expected, actual);
                    Assert.IsTrue(result.AreEqual, result.DifferencesString);
                }
            }
            catch (Exception e)
            {
                throw new Exception($"Exception during testing of protocol: {protocolType.FullName}", e);
            }
        }

        [DataTestMethod]
        [DataRow(typeof(TBinaryProtocol))]
        [DataRow(typeof(TCompactProtocol))]
        [DataRow(typeof(TJsonProtocol))]
        public async Task WriteReadI16_Test(Type protocolType)
        {
            var expected = short.MaxValue;

            try
            {
                var tuple = GetProtocolInstance(protocolType);
                using (var stream = tuple.Stream)
                {
                    var protocol = tuple.Protocol;

                    await protocol.WriteI16Async(expected, default);
                    await tuple.Transport.FlushAsync(default);

                    stream.Seek(0, SeekOrigin.Begin);

                    var actual = await protocol.ReadI16Async(default);

                    var result = _compareLogic.Compare(expected, actual);
                    Assert.IsTrue(result.AreEqual, result.DifferencesString);
                }
            }
            catch (Exception e)
            {
                throw new Exception($"Exception during testing of protocol: {protocolType.FullName}", e);
            }
        }

        [DataTestMethod]
        [DataRow(typeof(TBinaryProtocol))]
        [DataRow(typeof(TCompactProtocol))]
        [DataRow(typeof(TJsonProtocol))]
        public async Task WriteReadI32_Test(Type protocolType)
        {
            var expected = int.MaxValue;

            try
            {
                var tuple = GetProtocolInstance(protocolType);
                using (var stream = tuple.Stream)
                {
                    var protocol = tuple.Protocol;

                    await protocol.WriteI32Async(expected, default);
                    await tuple.Transport.FlushAsync(default);

                    stream.Seek(0, SeekOrigin.Begin);

                    var actual = await protocol.ReadI32Async(default);

                    var result = _compareLogic.Compare(expected, actual);
                    Assert.IsTrue(result.AreEqual, result.DifferencesString);
                }
            }
            catch (Exception e)
            {
                throw new Exception($"Exception during testing of protocol: {protocolType.FullName}", e);
            }
        }

        [DataTestMethod]
        [DataRow(typeof(TBinaryProtocol))]
        [DataRow(typeof(TCompactProtocol))]
        [DataRow(typeof(TJsonProtocol))]
        public async Task WriteReadI64_Test(Type protocolType)
        {
            var expected = long.MaxValue;

            try
            {
                var tuple = GetProtocolInstance(protocolType);
                using (var stream = tuple.Stream)
                {
                    var protocol = tuple.Protocol;

                    await protocol.WriteI64Async(expected, default);
                    await tuple.Transport.FlushAsync(default);

                    stream.Seek(0, SeekOrigin.Begin);

                    var actual = await protocol.ReadI64Async(default);

                    var result = _compareLogic.Compare(expected, actual);
                    Assert.IsTrue(result.AreEqual, result.DifferencesString);
                }
            }
            catch (Exception e)
            {
                throw new Exception($"Exception during testing of protocol: {protocolType.FullName}", e);
            }
        }

        [DataTestMethod]
        [DataRow(typeof(TBinaryProtocol))]
        [DataRow(typeof(TCompactProtocol))]
        [DataRow(typeof(TJsonProtocol))]
        public async Task WriteReadDouble_Test(Type protocolType)
        {
            var expected = double.MaxValue;

            try
            {
                var tuple = GetProtocolInstance(protocolType);
                using (var stream = tuple.Stream)
                {
                    var protocol = tuple.Protocol;

                    await protocol.WriteDoubleAsync(expected, default);
                    await tuple.Transport.FlushAsync(default);

                    stream.Seek(0, SeekOrigin.Begin);

                    var actual = await protocol.ReadDoubleAsync(default);

                    var result = _compareLogic.Compare(expected, actual);
                    Assert.IsTrue(result.AreEqual, result.DifferencesString);
                }
            }
            catch (Exception e)
            {
                throw new Exception($"Exception during testing of protocol: {protocolType.FullName}", e);
            }
        }

        [DataTestMethod]
        [DataRow(typeof(TBinaryProtocol))]
        [DataRow(typeof(TCompactProtocol))]
        [DataRow(typeof(TJsonProtocol))]
        public async Task WriteReadUuid_Test(Type protocolType)
        {
            var expected = new Guid("{00112233-4455-6677-8899-aabbccddeeff}");

            try
            {
                var tuple = GetProtocolInstance(protocolType);
                using (var stream = tuple.Stream)
                {
                    var protocol = tuple.Protocol;

                    await protocol.WriteUuidAsync(expected, default);
                    await tuple.Transport.FlushAsync(default);

                    stream.Seek(0, SeekOrigin.Begin);

                    var actual = await protocol.ReadUuidAsync(default);

                    var result = _compareLogic.Compare(expected, actual);
                    Assert.IsTrue(result.AreEqual, result.DifferencesString);
                }
            }
            catch (Exception e)
            {
                throw new Exception($"Exception during testing of protocol: {protocolType.FullName}", e);
            }
        }

        [DataTestMethod]
        [DataRow(typeof(TBinaryProtocol))]
        [DataRow(typeof(TCompactProtocol))]
        [DataRow(typeof(TJsonProtocol))]
        public async Task WriteReadString_Test(Type protocolType)
        {
            var expected = nameof(String);

            try
            {
                var tuple = GetProtocolInstance(protocolType);
                using (var stream = tuple.Stream)
                {
                    var protocol = tuple.Protocol;

                    await protocol.WriteStringAsync(expected, default);
                    await tuple.Transport.FlushAsync(default);

                    stream.Seek(0, SeekOrigin.Begin);

                    var actual = await protocol.ReadStringAsync(default);

                    var result = _compareLogic.Compare(expected, actual);
                    Assert.IsTrue(result.AreEqual, result.DifferencesString);
                }
            }
            catch (Exception e)
            {
                throw new Exception($"Exception during testing of protocol: {protocolType.FullName}", e);
            }
        }

        [DataTestMethod]
        [DataRow(typeof(TBinaryProtocol))]
        [DataRow(typeof(TCompactProtocol))]
        [DataRow(typeof(TJsonProtocol))]
        public async Task WriteReadBinary_Test(Type protocolType)
        {
            var expected = Encoding.UTF8.GetBytes(nameof(String));

            try
            {
                var tuple = GetProtocolInstance(protocolType);
                using (var stream = tuple.Stream)
                {
                    var protocol = tuple.Protocol;

                    await protocol.WriteBinaryAsync(expected, default);
                    await tuple.Transport.FlushAsync(default);

                    stream.Seek(0, SeekOrigin.Begin);

                    var actual = await protocol.ReadBinaryAsync(default);

                    var result = _compareLogic.Compare(expected, actual);
                    Assert.IsTrue(result.AreEqual, result.DifferencesString);
                }
            }
            catch (Exception e)
            {
                throw new Exception($"Exception during testing of protocol: {protocolType.FullName}", e);
            }
        }
    }
}
