import ApiError from "../utils/ApiError"
import ApiResponse from "../utils/ApiResponse"

const compressFile = async (req) => {
  const formdata = await req.formData()
  const file = formdata.get('file')

  if (!(file instanceof File)) {
    throw new ApiError(400, 'Upload a valid file')
  }

  return Response.json(
    new ApiResponse(
      200,
      {
        name: file.name,
        size: file.size
      },
      'file successfully uploaded'
    )
  )
}

export { compressFile }
