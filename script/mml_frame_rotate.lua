local function rotate_plane_lua(src_data, dst_data, stride, w, h, angle_rad, bg_color)
  
  local floor = math.floor
  local cos   = math.cos
  local sin   = math.sin

  local cos_a = cos(angle_rad)
  local sin_a = sin(angle_rad)

  local cx = w / 2.0
  local cy = h / 2.0

  -- Loop 0 to h-1 to keep math logic consistent with C
  for y = 0, h - 1 do
    for x = 0, w - 1 do
      
      local xt = x - cx
      local yt = y - cy

      local src_x = floor(xt * cos_a + yt * sin_a + cx)
      local src_y = floor(yt * cos_a - xt * sin_a + cy)

      -- Calculate Index (1-based for Lua Tables)
      local dst_idx = (y * stride + x) + 1

      if src_x >= 0 and src_x < w and src_y >= 0 and src_y < h then
        local src_idx = (src_y * stride + src_x) + 1
        local pixel_val = mem_read(src_data, src_idx)
        mem_write(dst_ptr, dst_idx, pixel_val)
      else
        mem_write(dst_ptr, dst_idx, bg_color)
      end
    end
  end
end

function mml_frame_effect(src_y, src_u, src_v, 
                          dst_y, dst_u, dst_v,
                          stride_y, stride_u, stride_v, 
                          width, height, offset_pts)
  local angle_rad = offset_pts * 3.1415926 / 180.0;
  rotate_plane_lua(src_y, dst_y, stride_y, width, height, angle_rad, 0);
  rotate_plane_lua(src_u, dst_u, stride_u, width / 2, height / 2, angle_rad, 128);
  rotate_plane_lua(src_v, dst_v, stride_v, width / 2, height / 2, angle_rad, 128);
end