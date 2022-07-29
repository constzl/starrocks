// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

package com.starrocks.fs.hdfs;

import com.starrocks.common.UserException;
import com.starrocks.thrift.TBrokerCheckPathExistRequest;
import com.starrocks.thrift.TBrokerCloseReaderRequest;
import com.starrocks.thrift.TBrokerCloseWriterRequest;
import com.starrocks.thrift.TBrokerDeletePathRequest;
import com.starrocks.thrift.TBrokerFD;
import com.starrocks.thrift.TBrokerFileStatus;
import com.starrocks.thrift.TBrokerListPathRequest;
import com.starrocks.thrift.TBrokerOpenReaderRequest;
import com.starrocks.thrift.TBrokerOpenWriterRequest;
import com.starrocks.thrift.TBrokerPReadRequest;
import com.starrocks.thrift.TBrokerPWriteRequest;
import com.starrocks.thrift.TBrokerRenamePathRequest;
import com.starrocks.thrift.TBrokerSeekRequest;
import com.starrocks.thrift.THdfsProperties;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

import java.util.List;
import java.util.Map;


public class HdfsService {

    private static Logger LOG = LogManager.getLogger(HdfsService.class);
    private HdfsFsManager fileSystemManager;

    public HdfsService() {
        fileSystemManager = new HdfsFsManager();
    }

    public void getTProperties(String path, Map<String, String> loadProperties, THdfsProperties tProperties)
            throws UserException {
        fileSystemManager.getTProperties(path, loadProperties, tProperties);
    }

    public void listPath(TBrokerListPathRequest request, List<TBrokerFileStatus> fileStatuses)
            throws UserException {
        LOG.info("received a list path request, request detail: " + request);
        boolean fileNameOnly = false;
        if (request.isSetFileNameOnly()) {
            fileNameOnly = request.isFileNameOnly();
        }
        List<TBrokerFileStatus> allFileStatuses = fileSystemManager.listPath(request.path, fileNameOnly,
                request.properties);

        for (TBrokerFileStatus tBrokerFileStatus : allFileStatuses) {
            if (tBrokerFileStatus.isDir) {
                continue;
            }
            fileStatuses.add(tBrokerFileStatus);
        }
    }

    public void deletePath(TBrokerDeletePathRequest request)
            throws UserException {
        LOG.info("receive a delete path request, request detail: " + request);
        fileSystemManager.deletePath(request.path, request.properties);
    }

    public void renamePath(TBrokerRenamePathRequest request)
            throws UserException {
        LOG.info("receive a rename path request, request detail: " + request);
        fileSystemManager.renamePath(request.srcPath, request.destPath, request.properties);
    }

    public boolean checkPathExist(
            TBrokerCheckPathExistRequest request) throws UserException {
        LOG.info("receive a check path request, request detail: " + request);
        return fileSystemManager.checkPathExist(request.path, request.properties);
    }

    public TBrokerFD openReader(TBrokerOpenReaderRequest request)
            throws UserException {
        LOG.info("receive a open reader request, request detail: " + request);
        return fileSystemManager.openReader(request.path,
                    request.startOffset, request.properties);
    }

    public byte[] pread(TBrokerPReadRequest request)
            throws UserException {
        LOG.debug("receive a read request, request detail: " + request);
        return fileSystemManager.pread(request.fd, request.offset, request.length);
    }

    public void seek(TBrokerSeekRequest request)
            throws UserException {
        LOG.debug("receive a seek request, request detail: " + request);
        fileSystemManager.seek(request.fd, request.offset);
    }

    public void closeReader(TBrokerCloseReaderRequest request)
            throws UserException {
        LOG.info("receive a close reader request, request detail: " + request);
        fileSystemManager.closeReader(request.fd);
    }

    public TBrokerFD openWriter(TBrokerOpenWriterRequest request)
            throws UserException {
        LOG.info("receive a open writer request, request detail: " + request);
        TBrokerFD fd = fileSystemManager.openWriter(request.path, request.properties);
        return fd;
    }

    public void pwrite(TBrokerPWriteRequest request)
            throws UserException {
        LOG.debug("receive a pwrite request, request detail: " + request);
        fileSystemManager.pwrite(request.fd, request.offset, request.getData());
    }

    public void closeWriter(TBrokerCloseWriterRequest request)
            throws UserException {
        LOG.info("receive a close writer request, request detail: " + request);
        fileSystemManager.closeWriter(request.fd);
    }
}
