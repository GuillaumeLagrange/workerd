#pragma once

#include <workerd/api/streams/writable.h>
#include <workerd/io/worker-fs.h>
#include <workerd/jsg/iterator.h>
#include <workerd/jsg/jsg.h>

namespace workerd::api {

class Blob;
class File;

// ======================================================================================
// An implementation of the WHATWG Web File System API (https://fs.spec.whatwg.org/)
// All of the classes in this part of the impl are defined by the spec.

class FileSystemSyncAccessHandle;
class FileSystemWritableFileStream;

class FileSystemHandle: public jsg::Object {
  // TODO(node-fs): The spec defines FileSystemHandle objects as being
  // serializable, meaning that they should work with structured cloning.
  // In workers, this would suggest also that they should work with jsrpc.
  // We don't yet implement any form of serialization for these objects.
  // Since each worker has its own file system, it might be a bit weird
  // as the received file on the other side would not actually be in that
  // destination worker's file system... that is, it would be an orphaned
  // file handle. It would still likely be useful tho. As a follow up
  // step, we will implement serialization/deserialization of these objects.
 public:
  FileSystemHandle(jsg::USVString name): name(kj::mv(name)) {}
  const jsg::USVString& getName(jsg::Lock& js) {
    return name;
  }

  virtual kj::StringPtr getKind(jsg::Lock& js) {
    // Implemented by subclasses.
    KJ_UNIMPLEMENTED("getKind() not implemented");
  }
  virtual jsg::Promise<bool> isSameEntry(jsg::Lock& js, jsg::Ref<FileSystemHandle> other) {
    // Implemented by subclasses.
    KJ_UNIMPLEMENTED("isSameEntry() not implemented");
  }

  JSG_RESOURCE_TYPE(FileSystemHandle) {
    JSG_READONLY_PROTOTYPE_PROPERTY(kind, getKind);
    JSG_READONLY_PROTOTYPE_PROPERTY(name, getName);
    JSG_METHOD(isSameEntry);
  }

 private:
  jsg::USVString name;
};

class FileSystemFileHandle: public FileSystemHandle {
 public:
  FileSystemFileHandle(jsg::USVString name, kj::Rc<workerd::File> inner);

  kj::StringPtr getKind(jsg::Lock& js) override {
    return "file"_kj;
  }

  jsg::Promise<bool> isSameEntry(jsg::Lock& js, jsg::Ref<FileSystemHandle> other) override;

  struct FileSystemCreateWritableOptions {
    bool keepExistingData = false;
    JSG_STRUCT(keepExistingData);
  };

  jsg::Promise<jsg::Ref<File>> getFile(jsg::Lock& js);

  jsg::Promise<jsg::Ref<FileSystemWritableFileStream>> createWritable(
      jsg::Lock& js, jsg::Optional<FileSystemCreateWritableOptions> options);

  jsg::Promise<jsg::Ref<FileSystemSyncAccessHandle>> createSyncAccessHandle(jsg::Lock& js);

  JSG_RESOURCE_TYPE(FileSystemFileHandle) {
    JSG_INHERIT(FileSystemHandle);
    JSG_METHOD(getFile);
    JSG_METHOD(createWritable);
    JSG_METHOD(createSyncAccessHandle);
  }

 private:
  kj::Rc<workerd::File> inner;
};

class FileSystemDirectoryHandle final: public FileSystemHandle {
  // TODO(node-fs): Per the spec, FileSystemDirectoryHandler objects are
  // expected to be async iterables. Here we implement it as a sync iterable.
  // The effect ends up being largely the same but it's obviously better to
  // use the asyn iterable API instead. We should implement this before we
  // ship this feature.
 public:
  struct EntryType {
    jsg::USVString key;
    jsg::Ref<FileSystemHandle> value;
    JSG_STRUCT(key, value);
    EntryType(jsg::USVString key, jsg::Ref<FileSystemHandle> value)
        : key(kj::mv(key)),
          value(kj::mv(value)) {}
  };

 private:
  using EntryIteratorType = EntryType;
  using KeyIteratorType = jsg::USVString;
  using ValueIteratorType = jsg::Ref<FileSystemHandle>;

  struct IteratorState final {
    jsg::Ref<FileSystemDirectoryHandle> parent;
    kj::Array<jsg::Ref<FileSystemHandle>> entries;
    uint index = 0;

    IteratorState(
        jsg::Ref<FileSystemDirectoryHandle> parent, kj::Array<jsg::Ref<FileSystemHandle>> entries)
        : parent(kj::mv(parent)),
          entries(kj::mv(entries)) {}

    void visitForGc(jsg::GcVisitor& visitor) {
      visitor.visit(parent);
      visitor.visitAll(entries);
    }

    JSG_MEMORY_INFO(IteratorState) {
      tracker.trackField("parent", parent);
      for (auto& entry: entries) {
        tracker.trackField("entry", entry);
      }
    }
  };

 public:
  FileSystemDirectoryHandle(jsg::USVString name, kj::Rc<workerd::Directory> inner);

  kj::StringPtr getKind(jsg::Lock& js) override {
    return "directory"_kj;
  }

  jsg::Promise<bool> isSameEntry(jsg::Lock& js, jsg::Ref<FileSystemHandle> other) override;

  struct FileSystemGetFileOptions {
    bool create = false;
    JSG_STRUCT(create);
  };

  struct FileSystemGetDirectoryOptions {
    bool create = false;
    JSG_STRUCT(create);
  };

  struct FileSystemRemoveOptions {
    bool recursive = false;
    JSG_STRUCT(recursive);
  };

  jsg::Promise<jsg::Ref<FileSystemFileHandle>> getFileHandle(jsg::Lock& js,
      jsg::USVString name,
      jsg::Optional<FileSystemGetFileOptions> options,
      const jsg::TypeHandler<jsg::Ref<jsg::DOMException>>& exception);

  jsg::Promise<jsg::Ref<FileSystemDirectoryHandle>> getDirectoryHandle(jsg::Lock& js,
      jsg::USVString name,
      jsg::Optional<FileSystemGetDirectoryOptions> options,
      const jsg::TypeHandler<jsg::Ref<jsg::DOMException>>& exception);

  jsg::Promise<void> removeEntry(jsg::Lock& js,
      jsg::USVString name,
      jsg::Optional<FileSystemRemoveOptions> options,
      const jsg::TypeHandler<jsg::Ref<jsg::DOMException>>& exception);

  // TODO(node-fs): We are not currently implementing the resolve() method
  // as defined in the spec. This is a bit tricky as it requires us to work
  // backwards through the parent directories to find the path to the file.
  // However, our file node's do not currently have a parent pointer, nor do
  // they know through which path they are accessible through. Support for this
  // is not critical so we likely won't implement this before we ship the feature
  // but it is something we should consider doing in the future.
  jsg::Promise<kj::Array<jsg::USVString>> resolve(
      jsg::Lock& js, jsg::Ref<FileSystemHandle> possibleDescendant);

  JSG_ITERATOR(
      EntryIterator, entries, EntryIteratorType, IteratorState, iteratorNext<EntryIteratorType>);
  JSG_ITERATOR(KeyIterator, keys, KeyIteratorType, IteratorState, iteratorNext<KeyIteratorType>);
  JSG_ITERATOR(
      ValueIterator, values, ValueIteratorType, IteratorState, iteratorNext<ValueIteratorType>);

  void forEach(jsg::Lock& js,
      jsg::Function<void(
          jsg::USVString, jsg::Ref<FileSystemHandle>, jsg::Ref<FileSystemDirectoryHandle>)>
          callback,
      jsg::Optional<jsg::Value> thisArg);

  JSG_RESOURCE_TYPE(FileSystemDirectoryHandle) {
    JSG_INHERIT(FileSystemHandle);
    JSG_METHOD(getFileHandle);
    JSG_METHOD(getDirectoryHandle);
    JSG_METHOD(removeEntry);
    JSG_METHOD(resolve);
    JSG_METHOD(entries);
    JSG_METHOD(keys);
    JSG_METHOD(values);
    JSG_ITERABLE(entries);
    JSG_METHOD(forEach);
  }

 private:
  kj::Rc<workerd::Directory> inner;

  template <typename Type>
  static kj::Maybe<Type> iteratorNext(jsg::Lock& js, IteratorState& state) {
    if (state.index >= state.entries.size()) {
      return kj::none;
    }

    auto& entry = state.entries[state.index++];

    if constexpr (kj::isSameType<Type, EntryIteratorType>()) {
      return EntryType(js.accountedUSVString(entry->getName(js)), entry.addRef());
    } else if constexpr (kj::isSameType<Type, KeyIteratorType>()) {
      return js.accountedUSVString(entry->getName(js));
    } else if constexpr (kj::isSameType<Type, ValueIteratorType>()) {
      return entry.addRef();
    } else {
      static_assert(false, "invalid iterator type");
    }
  }
};

class FileSystemWritableFileStream: public WritableStream {
  // TODO(node-fs): The spec defines FileSystemWritableFileStream objects such
  // that any changes to the file are not actually committed until the stream
  // is closed. We're not yet implementing this. All writes modify the source
  // file in memory and would be immediately visible to other handles pointing
  // to the same file. This should be updated before this feature is unflagged
  // and made stable.
 public:
  struct State: public kj::Refcounted {
    kj::Maybe<kj::Rc<workerd::File>> file;
    size_t position = 0;
    State(kj::Rc<workerd::File> file): file(kj::mv(file)) {}
  };

  FileSystemWritableFileStream(
      kj::Own<WritableStreamController> controller, kj::Rc<State> sharedState);

  static jsg::Ref<FileSystemWritableFileStream> constructor() = delete;

  struct WriteParams {
    kj::String type;  // one of: write, seek, truncate
    jsg::Optional<double> size;
    jsg::Optional<double> position;
    jsg::Optional<kj::OneOf<jsg::Ref<Blob>, jsg::BufferSource, kj::String>> data;
    JSG_STRUCT(type, size, position, data);
  };

  jsg::Promise<void> write(
      jsg::Lock& js, kj::OneOf<jsg::Ref<Blob>, jsg::BufferSource, kj::String, WriteParams> data);
  jsg::Promise<void> seek(jsg::Lock& js, double position);
  jsg::Promise<void> truncate(jsg::Lock& js, double size);

  JSG_RESOURCE_TYPE(FileSystemWritableFileStream) {
    JSG_INHERIT(WritableStream);
    JSG_METHOD(write);
    JSG_METHOD(seek);
    JSG_METHOD(truncate);
  }

 private:
  kj::Rc<State> sharedState;
};

class FileSystemSyncAccessHandle final: public jsg::Object {
 public:
  FileSystemSyncAccessHandle(kj::Rc<workerd::File> inner);

  struct FileSystemReadWriteOptions {
    jsg::Optional<double> at;
    JSG_STRUCT(at);
  };

  double read(
      jsg::Lock& js, jsg::BufferSource buffer, jsg::Optional<FileSystemReadWriteOptions> options);
  double write(
      jsg::Lock& js, jsg::BufferSource buffer, jsg::Optional<FileSystemReadWriteOptions> options);

  void truncate(jsg::Lock& js, double newSize);
  double getSize(jsg::Lock& js);
  void flush(jsg::Lock& js);
  void close(jsg::Lock& js);

  JSG_RESOURCE_TYPE(FileSystemSyncAccessHandle) {
    JSG_METHOD(read);
    JSG_METHOD(write);
    JSG_METHOD(truncate);
    JSG_METHOD(getSize);
    JSG_METHOD(flush);
    JSG_METHOD(close);
  }

 private:
  kj::Maybe<kj::Rc<workerd::File>> inner;
  size_t position = 0;
};

class StorageManager final: public jsg::Object {
 public:
  jsg::Promise<jsg::Ref<FileSystemDirectoryHandle>> getDirectory(
      jsg::Lock& js, const jsg::TypeHandler<jsg::Ref<jsg::DOMException>>& exception);

  JSG_RESOURCE_TYPE(StorageManager) {
    JSG_METHOD(getDirectory);
  }
};

#define EW_WEB_FILESYSTEM_ISOLATE_TYPE                                                             \
  workerd::api::FileSystemHandle, workerd::api::FileSystemFileHandle,                              \
      workerd::api::FileSystemDirectoryHandle, workerd::api::FileSystemWritableFileStream,         \
      workerd::api::FileSystemSyncAccessHandle, workerd::api::StorageManager,                      \
      workerd::api::FileSystemFileHandle::FileSystemCreateWritableOptions,                         \
      workerd::api::FileSystemDirectoryHandle::FileSystemGetFileOptions,                           \
      workerd::api::FileSystemDirectoryHandle::FileSystemGetDirectoryOptions,                      \
      workerd::api::FileSystemDirectoryHandle::FileSystemRemoveOptions,                            \
      workerd::api::FileSystemSyncAccessHandle::FileSystemReadWriteOptions,                        \
      workerd::api::FileSystemWritableFileStream::WriteParams,                                     \
      workerd::api::FileSystemDirectoryHandle::EntryType,                                          \
      workerd::api::FileSystemDirectoryHandle::EntryIterator,                                      \
      workerd::api::FileSystemDirectoryHandle::KeyIterator,                                        \
      workerd::api::FileSystemDirectoryHandle::ValueIterator,                                      \
      workerd::api::FileSystemDirectoryHandle::EntryIterator::Next,                                \
      workerd::api::FileSystemDirectoryHandle::KeyIterator::Next,                                  \
      workerd::api::FileSystemDirectoryHandle::ValueIterator::Next

}  // namespace workerd::api
