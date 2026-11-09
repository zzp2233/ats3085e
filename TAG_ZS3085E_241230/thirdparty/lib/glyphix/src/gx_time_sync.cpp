#include <cassert>
#include <mas_rpc.h>
#include <gx_logger.h>
#include <cJSON.h>
#include <time.h>



struct mas_rpc_blob* time_sync_svc(struct mas_rpc_blob* input)
{
    cJSON* root = nullptr;
    const cJSON* item_timestamp = nullptr;
    const cJSON* item_timezone = nullptr;
    const cJSON* item_minute_offset = nullptr;
    time_t timestamp = 0;
    int timezone = 0;
    int minute_offset = 0;
    struct mas_rpc_blob* output_blob = mas_rpc_create_result_blob(GX_ERROR);
    assert(output_blob);

    // the input object is empty, return directly.
    if ((input == nullptr) || (input->buf == nullptr))
    {
        gx::LogError() << "time sync failed, input is null.";
        return output_blob;
    }
    // input object converted to json object.
    
    root = mas_rpc_blob_to_json(input, root);
    if (root == nullptr)
    {
        gx::LogError() << "time sync failed, format input to json failed.";
        return output_blob;
    }
    // root is not empty.
    item_timestamp = cJSON_GetObjectItem(root, "timestamp");
    item_timezone = cJSON_GetObjectItem(root, "timezone");
    item_minute_offset = cJSON_GetObjectItem(root, "minute_offset");
    // is the json object correct.
    if ((item_timestamp == nullptr) || (cJSON_IsNumber(item_timestamp) == false))
    {
        gx::LogError() << "time sync failed, args timestamp is not a number.";
        cJSON_Delete(root);
        return output_blob;
    }
    else
    {
        timestamp = (time_t)item_timestamp->valueint;
    }
    if ((item_timezone == nullptr) || (cJSON_IsNumber(item_timezone) == false))
    {
        gx::LogError() << "time sync failed, args timezone is not a number.";
        cJSON_Delete(root);
        return output_blob;
    }
    else
    {
        timezone = (int)item_timezone->valueint;
    }
    if ((item_minute_offset == nullptr) || (cJSON_IsNumber(item_minute_offset) == false))
    {
        minute_offset = 0;
    }
    else
    {
        minute_offset = (int)item_minute_offset->valueint;
    }
    gx::LogInfo() << "time sync svc timestamp:" << timestamp << ", timezone:" << timezone << ", minute offset:" <<  minute_offset;
    // sync the time.
    timespec new_time;
    new_time.tv_sec = timestamp;
    new_time.tv_nsec = 0;

    if(clock_settime(CLOCK_REALTIME, &new_time) == -1){
        gx::LogError() << "clock settime failed";
        return output_blob;
    }
    // release the json object.
    cJSON_Delete(root);
    // return result blob
    mas_rpc_blob_free(output_blob);
    output_blob = mas_rpc_create_result_blob(GX_EOK);
    assert(output_blob);
    return output_blob;
}

extern "C" void gx_time_sync_init(){
    gx::LogInfo() << "register time service";
    mas_rpc_svc_register("time_sync_svc", time_sync_svc);
}
