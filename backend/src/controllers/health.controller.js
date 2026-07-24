import ApiResponse from "../utils/ApiResponse"

const health = ()  => {
  return Response.json(new ApiResponse(200, {}, 'server is healthy'))
}

export { health }
