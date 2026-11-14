<img width="1642" height="845" alt="image" src="https://github.com/user-attachments/assets/a5ecb959-31c3-43c2-8455-b6008b5664f5" />
# Vulkan 복셀 레이 트레이싱 엔진 ($32^3$ Chunk Pipeline)
Vulkan Ray Query와 마칭 큐브(Marching Cubes) 컴퓨트 파이프라인을 결합하여 실시간 그림자를 구현한 복셀 렌더링 엔진입니다.
#주요 특징 및 구조
- 복셀 청크 설계
  - $32 \times 32 \times 32$ 크기의 표준 복셀 청크($12 \times 4 \times 12$ 그리드, 총 $576$개)를 사용하여 공간 분할 효율과 BLAS 리빌드 속도의 균형을 맞췄습니다                                                      
