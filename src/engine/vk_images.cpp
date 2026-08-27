#include <algorithm>
#include <cmath>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"


#include "../../header/vk_initializers.h"
#include "../../header/vk_images.h"

//(command들이 들어있는 공간, 원본 이미지, 기존 layout, 원하는 layout)
void vkutil::transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout)
{
    //실제 pipeline barrier이다
    VkImageMemoryBarrier2 imageBarrier {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    imageBarrier.pNext = nullptr;
    //앞에 지시했던 커멘드들이 끝날때까지 기다린다
    imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    //앞에가 끝나면 VRAM을 다 비운다
    imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;

    //현제 작업이 끝날때가지 미래 작업들을 대기 시킨다
    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    //현제 작업이 끝나고 다음 미래 작업들이 볼수 있게 생팅
    imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

    //현제 layout과 newLayout을 기록
    imageBarrier.oldLayout = currentLayout;
    imageBarrier.newLayout = newLayout;

    //그냥 평범한 도화지인지 아니면 depth 도화지인지 구별
    VkImageAspectFlags aspectMask = (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

    //여기는 mipmap 관련된 코드니 지금은 건드리지 않겠다
    imageBarrier.subresourceRange = vkinit::image_subresource_range(aspectMask);
    //image에다가 이제 연결
    imageBarrier.image = image;

    //배리어의 대한 정보
    VkDependencyInfo depInfo {};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.pNext = nullptr;

    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &imageBarrier;

    //실제로 만들기
    vkCmdPipelineBarrier2(cmd, &depInfo);
}

void vkutil::copy_image_to_image(VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent2D srcSize, VkExtent2D dstSize)
{
    //이미지 복사 하지만 크기를 상황에 따라서 바꿀 수 있다.
    VkImageBlit2 blitRegion{ .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2, .pNext = nullptr };

    //실제 복사하는 이미지의 크기
    blitRegion.srcOffsets[1].x = srcSize.width;
    blitRegion.srcOffsets[1].y = srcSize.height;
    blitRegion.srcOffsets[1].z = 1;

    blitRegion.dstOffsets[1].x = dstSize.width;
    blitRegion.dstOffsets[1].y = dstSize.height;
    blitRegion.dstOffsets[1].z = 1;

    //사용하고 싶은 레이어나 mipmap
    blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.srcSubresource.baseArrayLayer = 0;
    blitRegion.srcSubresource.layerCount = 1;
    blitRegion.srcSubresource.mipLevel = 0;

    //비슷한 내용이다 (어느 mipmap을 쓸것인가??)
    blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.dstSubresource.baseArrayLayer = 0;
    blitRegion.dstSubresource.layerCount = 1;
    blitRegion.dstSubresource.mipLevel = 0;

    VkBlitImageInfo2 blitInfo{ .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2, .pNext = nullptr };
    blitInfo.dstImage = destination;
    //layout은 목적지로 보내기 편한 layout
    blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blitInfo.srcImage = source;

    //layout은 현제 복사하기 가장 편하게 바꿔줘
    blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    //이미지가 중간에 크기가 바뀌면 모니터에 맞게 부드럽게 늘려줘
    blitInfo.filter = VK_FILTER_LINEAR;
    blitInfo.regionCount = 1;
    blitInfo.pRegions = &blitRegion;

    vkCmdBlitImage2(cmd, &blitInfo);
}
void vkutil::generate_mipmaps(VkCommandBuffer cmd, VkImage image, VkExtent2D imageSize)
{
    //여기서 이제 mipmap level들의 개수를 생성을 한다
    //총 밉맵이 몇게 만들어질지 결정을 하는 코드라고 보면 빠르다
    int mipLevels = int(std::floor(std::log2(std::max(imageSize.width, imageSize.height)))) + 1;
    //mip level만큼 반복을 한다
    for (int mip = 0; mip < mipLevels; mip++) {
	//일단 이미지의 1/4 사이즈를 확보를 한다
        VkExtent2D halfSize = imageSize;
        halfSize.width /= 2;
        halfSize.height /= 2;

        VkImageMemoryBarrier2 imageBarrier { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .pNext = nullptr };

	//누군가 건내줄때 쓰고 있던 데이터를 받을것이다
	//WRITE_BIT
        imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;

	//반면 우리가 넘겨줄떄는 오히려 읽거나, 쓰는형태로 넘겨줄것이다 그때까지 GPU에서 이거 관련된걸 멈춰
        imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

	// [이전 상태] 다른 곳(버퍼/상위 밉맵)으로부터 데이터를 '복사받던' 목적지(Destination) 도화지 상태
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        // [바뀔 상태] 이제 생성이 끝났으니, 다음 단계(하위 밉맵 생성/샘플링)를 위해 데이터를 '읽어주는' 원본(Source) 상태로 전환!
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

	//어떤 이미지가 현제 쓰이느냐를 판단하는 코드이다
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageBarrier.subresourceRange = vkinit::image_subresource_range(aspectMask);
        imageBarrier.subresourceRange.levelCount = 1;
	//현제 우리 이미지 MipLevels를 설정을 해주는 거임(0,1,2,3..)
        imageBarrier.subresourceRange.baseMipLevel = mip;
        imageBarrier.image = image;

	//이제 파이프라인 배리어를 만드는거임
	//현제 우리 바리케이드는 cmd중에서 VK_ACCESS_2_MEMORY_WRITE_BIT을 기다리고 있어
	//기억나?? 우리는 이걸 받기로 했잖아 그동안 이 작업은 하지 말라는 의미야
	//그리고 VK_SRC가 WRITE혹은READ로 바뀌었을때 내보네
        VkDependencyInfo depInfo { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .pNext = nullptr };
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &imageBarrier;
        vkCmdPipelineBarrier2(cmd, &depInfo);

        if (mip < mipLevels - 1) {
            VkImageBlit2 blitRegion { .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2, .pNext = nullptr };

	    //원본 이미지
            blitRegion.srcOffsets[1].x = imageSize.width;
            blitRegion.srcOffsets[1].y = imageSize.height;
            blitRegion.srcOffsets[1].z = 1;

	    //한층 더 작은 이미지
            blitRegion.dstOffsets[1].x = halfSize.width;
            blitRegion.dstOffsets[1].y = halfSize.height;
            blitRegion.dstOffsets[1].z = 1;

	    //원본 이미지 설정
            blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blitRegion.srcSubresource.baseArrayLayer = 0;
            blitRegion.srcSubresource.layerCount = 1;
            blitRegion.srcSubresource.mipLevel = mip;

	    //한층 더 작은 이미지 설정
            blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blitRegion.dstSubresource.baseArrayLayer = 0;
            blitRegion.dstSubresource.layerCount = 1;
            blitRegion.dstSubresource.mipLevel = mip + 1;

            VkBlitImageInfo2 blitInfo {.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2, .pNext = nullptr};
	    //현제 상태를 보고 이미지를 dstImage에다가 삽입
            blitInfo.dstImage = image;
	    //현제 받을 이미지이고 현제 받을 상태
            blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	    //****상태를 보고 우리가 끝난 데이터를 넣어놀 공간--> 여기로 다시 대입
	    //mipmap은 좀 특수해서 이미지 여러개를 덮어써도 vulkan 읽을수 있음
            blitInfo.srcImage = image;
	    //내보내기 위한 준비를 하는것이다(원본 상태로 전환될때 srcImage에다가 대입)
            blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            blitInfo.filter = VK_FILTER_LINEAR;
            blitInfo.regionCount = 1;
            blitInfo.pRegions = &blitRegion;

	    //이미지 복사(여기서 우리가 파라미터로 받아온 image 에다가 복사를 함)
            vkCmdBlitImage2(cmd, &blitInfo);

            imageSize = halfSize;
        }
    }

    //복사가 끝나면 image데이터를 쉐이더가 읽을 수 있겠끔 다시 만들자
    transition_image(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
