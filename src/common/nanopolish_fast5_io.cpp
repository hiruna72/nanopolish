//---------------------------------------------------------
// Copyright 2018 Ontario Institute for Cancer Research
// Written by Jared Simpson (jared.simpson@oicr.on.ca)
//---------------------------------------------------------
//
// nanopolish_fast5_io -- lightweight functions
// to read specific data from fast5 files
//
#include <string.h>
#include <math.h>
#include <assert.h>
#include "nanopolish_fast5_io.h"

#define DEBUG_FAST5_IO 1

#define RAW_ROOT "/Raw/Reads/"
int verbose = 0;

//
fast5_file fast5_open(const std::string& filename)
{
    fast5_file fh;
    fh.hdf5_file = H5Fopen(filename.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);

    // read and parse the file version
    std::string version_str = fast5_get_string_attribute(fh, "/", "file_version");
    //fprintf(stderr, "file version: %s\n", version_str.c_str());
    return fh;
}

//
bool fast5_is_open(fast5_file& fh)
{
    return fh.hdf5_file >= 0;
}

//
void fast5_close(fast5_file& fh)
{
    H5Fclose(fh.hdf5_file);
}

//
std::string fast5_get_raw_read_group(fast5_file& fh)
{
    std::string read_name = fast5_get_raw_read_name(fh);
    if(read_name != "") {
        return std::string(RAW_ROOT) + read_name;
    } else {
        return "";
    }
}

//
std::string fast5_get_raw_read_name(fast5_file& fh)
{
    // This code is From scrappie's fast5_interface

    // retrieve the size of the read name
    ssize_t size =
        H5Lget_name_by_idx(fh.hdf5_file, RAW_ROOT, H5_INDEX_NAME, H5_ITER_INC, 0, NULL,
                           0, H5P_DEFAULT);

    if (size < 0) {
        return "";
    }

    // copy the read name out of the fast5
    char* name = (char*)calloc(1 + size, sizeof(char));
    H5Lget_name_by_idx(fh.hdf5_file, RAW_ROOT, H5_INDEX_NAME, H5_ITER_INC, 0, name,
                       1 + size, H5P_DEFAULT);

    // cleanup
    std::string out(name);
    free(name);
    return out;
}

//
std::string fast5_get_read_id(fast5_file& fh)
{
    int ret;
    hid_t read_name_attribute, raw_group, attribute_type;
    size_t storage_size = 0;
    char* read_name_str = NULL;

    std::string out = "";
    
    // Get the path to the raw read group
    std::string raw_read_group = fast5_get_raw_read_group(fh);
    if(raw_read_group == "") {
        return out;
    }

    return fast5_get_string_attribute(fh, raw_read_group, "read_id");
}

//
raw_table fast5_get_raw_samples(fast5_file& fh, fast5_raw_scaling scaling)
{
    float* rawptr = NULL;
    hid_t space;
    hsize_t nsample;
    herr_t status;
    float raw_unit;
    raw_table rawtbl = { 0, 0, 0, NULL };

    // mostly from scrappie
    std::string raw_read_group = fast5_get_raw_read_group(fh);

    // Create data set name
    std::string signal_path = raw_read_group + "/Signal";

    hid_t dset = H5Dopen(fh.hdf5_file, signal_path.c_str(), H5P_DEFAULT);
    if (dset < 0) {
#ifdef DEBUG_FAST5_IO
        fprintf(stderr, "Failed to open dataset '%s' to read raw signal from.\n", signal_path.c_str());
#endif
        goto cleanup2;
    }

    space = H5Dget_space(dset);
    if (space < 0) {
        fprintf(stderr, "Failed to create copy of dataspace for raw signal %s.\n", signal_path.c_str());
        goto cleanup3;
    }

    H5Sget_simple_extent_dims(space, &nsample, NULL);
    rawptr = (float*)calloc(nsample, sizeof(float));
    status = H5Dread(dset, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT, rawptr);

    if (status < 0) {
        free(rawptr);
#ifdef DEBUG_FAST5_IO
        fprintf(stderr, "Failed to read raw data from dataset %s.\n", signal_path.c_str());
#endif
        goto cleanup4;
    }

    // convert to pA
    rawtbl = (raw_table) { nsample, 0, nsample, rawptr };
    raw_unit = scaling.range / scaling.digitisation;
    for (size_t i = 0; i < nsample; i++) {
        rawptr[i] = (rawptr[i] + scaling.offset) * raw_unit;
    }

 cleanup4:
    H5Sclose(space);
 cleanup3:
    H5Dclose(dset);
 cleanup2:
    return rawtbl;
}

//
std::string fast5_get_experiment_type(fast5_file& fh)
{
    return fast5_get_string_attribute(fh, "/UniqueGlobalKey/context_tags", "experiment_type");
}

// from scrappie
float fast5_read_float_attribute(hid_t group, const char *attribute) {
    float val = NAN;
    if (group < 0) {
#ifdef DEBUG_FAST5_IO
        fprintf(stderr, "Invalid group passed to %s:%d.", __FILE__, __LINE__);
#endif
        return val;
    }

    hid_t attr = H5Aopen(group, attribute, H5P_DEFAULT);
    if (attr < 0) {
#ifdef DEBUG_FAST5_IO
        fprintf(stderr, "Failed to open attribute '%s' for reading.", attribute);
#endif
        return val;
    }

    H5Aread(attr, H5T_NATIVE_FLOAT, &val);
    H5Aclose(attr);

    return val;
}

//
fast5_raw_scaling fast5_get_channel_params(fast5_file& fh)
{
    // from scrappie
    fast5_raw_scaling scaling = { NAN, NAN, NAN, NAN };
    const char *scaling_path = "/UniqueGlobalKey/channel_id";

    hid_t scaling_group = H5Gopen(fh.hdf5_file, scaling_path, H5P_DEFAULT);
    if (scaling_group < 0) {
#ifdef DEBUG_FAST5_IO
        fprintf(stderr, "Failed to group %s.", scaling_path);
#endif
        return scaling;
    }

    scaling.digitisation = fast5_read_float_attribute(scaling_group, "digitisation");
    scaling.offset = fast5_read_float_attribute(scaling_group, "offset");
    scaling.range = fast5_read_float_attribute(scaling_group, "range");
    scaling.sample_rate = fast5_read_float_attribute(scaling_group, "sampling_rate");

    H5Gclose(scaling_group);

    return scaling;
}

//
// Internal functions
//

//
std::string fast5_get_string_attribute(fast5_file& fh, const std::string& group_name, const std::string& attribute_name)
{
    hid_t group, attribute, attribute_type, native_type;
    std::string out;

    // according to http://hdf-forum.184993.n3.nabble.com/check-if-dataset-exists-td194725.html
    // we should use H5Lexists to check for the existence of a group/dataset using an arbitrary path
    // HDF5 1.8 returns 0 on the root group, so we explicitly check for it
    int ret = group_name == "/" ? 1 : H5Lexists(fh.hdf5_file, group_name.c_str(), H5P_DEFAULT);
    if(ret <= 0) {
        return "";
    }

    // Open the group containing the attribute we want
    group = H5Gopen(fh.hdf5_file, group_name.c_str(), H5P_DEFAULT);
    if(group < 0) {
#ifdef DEBUG_FAST5_IO
        fprintf(stderr, "could not open group %s\n", group_name.c_str());
#endif
        goto close_group;
    }

    // Ensure attribute exists
    ret = H5Aexists(group, attribute_name.c_str());
    if(ret <= 0) {
        goto close_group;
    }

    // Open the attribute
    attribute = H5Aopen(group, attribute_name.c_str(), H5P_DEFAULT);
    if(attribute < 0) {
#ifdef DEBUG_FAST5_IO
        fprintf(stderr, "could not open attribute: %s\n", attribute_name.c_str());
#endif
        goto close_attr;
    }

    // Get data type and check it is a fixed-length string
    attribute_type = H5Aget_type(attribute);
    if(attribute_type < 0) {
#ifdef DEBUG_FAST5_IO
        fprintf(stderr, "failed to get attribute type %s\n", attribute_name.c_str());
#endif
        goto close_type;
    }

    if(H5Tget_class(attribute_type) != H5T_STRING) {
#ifdef DEBUG_FAST5_IO
        fprintf(stderr, "attribute %s is not a string\n", attribute_name.c_str());
#endif
        goto close_type;
    }

    native_type = H5Tget_native_type(attribute_type, H5T_DIR_ASCEND);
    if(native_type < 0) {
#ifdef DEBUG_FAST5_IO
        fprintf(stderr, "failed to get native type for %s\n", attribute_name.c_str());
#endif
        goto close_native_type;
    }

    if(H5Tis_variable_str(attribute_type) > 0) {
        // variable length string
        char* buffer;
        ret = H5Aread(attribute, native_type, &buffer);
        if(ret < 0) {
            fprintf(stderr, "error reading attribute %s\n", attribute_name.c_str());
            exit(EXIT_FAILURE);
        }
        out = buffer;
        free(buffer);
        buffer = NULL;

    } else {
        // fixed length string
        size_t storage_size;
        char* buffer;

        // Get the storage size and allocate
        storage_size = H5Aget_storage_size(attribute);
        buffer = (char*)calloc(storage_size + 1, sizeof(char));

        // finally read the attribute
        ret = H5Aread(attribute, attribute_type, buffer);
        if(ret >= 0) {
            out = buffer;
        }

        // clean up
        free(buffer);
    }

close_native_type:
    H5Tclose(native_type);    
close_type:
    H5Tclose(attribute_type);
close_attr:
    H5Aclose(attribute);
close_group:
    H5Gclose(group);

    return out;
}
