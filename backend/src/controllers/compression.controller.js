import { CORS_HEADERS, MAX_FILE_SIZE } from "../constants"
import { compress } from "../services/compression.service"
import ApiError from "../utils/ApiError"
import ApiResponse from "../utils/ApiResponse"

const compressFile = async (req) => {
  const formdata = await req.formData()
  const file = await formdata.get('file')

  if (!(file instanceof File)) {
    throw new ApiError(400, 'Upload a valid file')
  }

  if (file.size > MAX_FILE_SIZE) {
    throw new ApiError(413, `Max upload size is ${ MAX_FILE_SIZE / 1024 / 1024 } MiB`)
  }

  const compressionResponse = await compress(file)

  return Response.json(
    new ApiResponse(
      200,
      compressionResponse,
      'file successfully compressed'
    ),
    {
      headers: CORS_HEADERS
    }
  )
}

export { compressFile }
