// 蓝脊灵豚 3D 呈现系统
class BlueRidgeSystem {
    constructor() {
        this.scene = null;
        this.camera = null;
        this.renderer = null;
        this.controls = null;
        this.pointCloudBatches = [];
        this.tracePoints = null;
        this.animationId = null;
        this.timeChart = null;
        this.sensorData = {
            depth: null, temperature: null, dissolved_oxygen: null,
            ph: null, conductivity: null, turbidity: null
        };
        this.sensorRanges = {};
        this.currentLakeRanges = {
            depth: { min: 45, max: 120 },
            temperature: { min: 0, max: 6 },
            dissolved_oxygen: { min: 10, max: 14 },
            ph: { min: 7.5, max: 8.5 },
            conductivity: { min: 50, max: 150 },
            turbidity: { min: 0.1, max: 1.0 }
        };
        // 动态范围存储（自动对比度模式使用）
        this.dynamicRanges = {};

        this.currentVisualizationMode = 'depth';
        this.dataLoaded = false;
        // 添加API基础URL配置
        this.apiBaseUrl = '';  // 使用相对路径，浏览器自动匹配当前域名和端口
        // 如果前后端同域名，使用相对路径：this.apiBaseUrl = '';
        this.dbConfig = {
            host: '47.108.232.40', user: 'qwe123',
            port: 3306, database: 'qwe123'
        };
        this.currentBatchPage = 1;
        this.isLoadingMore = false;
        this.maxBatches = 100;              
        this.batchSize = 10000;            
        this.currentLOD = 1;
        this.lodTimeout = null;

        this.loadRetryCount = 0;
        this.maxRetries = 3;

        this.renderStats = { totalPoints: 0, renderedPoints: 0, lastLoadTime: 0 };
        this.currentLake = null;
        this.lakeData = {};
        this.lastMouseMoveTime = Date.now();
        this.currentTimeRange = '1d';

        this.colorStats = {
            minNorm: 1, maxNorm: 0, avgNorm: 0, normSum: 0, normCount: 0
        };

        this.colorCache = new Map();
        this.maxColorCacheSize = 10000;
        
        // ============ 切面功能属性 ============
        this.clippingEnabled = false;
        this.clippingPlane = null;
        this.clippingPlaneMesh = null;
        this.currentClipAxis = 'Z'; 
        this.currentClipPosition = 0;
        this.clippingPlanes = [];
        
        // 点云边界（用于自适应切面）
        this.pointCloudBounds = {
    minX: -500, maxX: 500,   
    minY: -500, maxY: 500,
    minZ: -500, maxZ: 500
};

        // ============ 轨迹交互属性 ============
        this.tracePointsData = []; // 存储轨迹点数据用于交互
        this.traceMarkers = []; // 轨迹点交互标记
        this.raycaster = new THREE.Raycaster();
        this.mouse = new THREE.Vector2();
        this.hoveredTracePoint = null;
        // 轨迹线悬停检测相关
        this.traceLineForRaycast = null; // 用于射线检测的轨迹线
        this.hoveredTraceSegment = null;

        // 自动对比度开关
        this.useAutoContrast = false;
        // 对比度增强系数（伽马校正）
        this.contrastGamma = 1.5;

        // AI助手相关
        this.aiContext = [];
        this.isAIProcessing = false;

        // 传感器数据有效性标志
        this.hasValidSensorData = false;
        
        // ============ 动态点云关联限制 ============
        this.adaptivePointLimit = {
            baseLimit: 50000,      // 基础限制
            maxLimit: 200000,      // 最大限制
            minLimit: 10000,       // 最小限制
            currentLimit: 50000    // 当前动态限制
        };
        
        // 自适应距离阈值
        this.adaptiveDistanceThreshold = {
            baseThreshold: 50,     // 基础阈值(米)
            minThreshold: 10,      // 最小阈值
            maxThreshold: 200,     // 最大阈值
            currentThreshold: 50   // 当前阈值
        };

        this.init();
    }

    init() {
        this.checkAuth();
        this.initThreeJS();
        this.initEventListeners();
        this.initCharts();
        this.initDatabaseConnection();
        this.initAIAssistant();
        this.showLoading(false);
    }

    checkAuth() {
        const user = sessionStorage.getItem('user');
        if (!user) { window.location.href = 'index.html'; return; }
        document.getElementById('currentUser').textContent = JSON.parse(user).username;
    }

    initThreeJS() {
        const container = document.getElementById('threejs-container');
        this.scene = new THREE.Scene();
        this.scene.background = new THREE.Color(0x0B1426);

        this.camera = new THREE.PerspectiveCamera(75, container.clientWidth / container.clientHeight, 0.1, 5000);
        // ============ 调整相机初始位置，X指向用户 ============
        this.camera.position.set(100, 50, 100);

        this.renderer = new THREE.WebGLRenderer({ antialias: true });
        this.renderer.setSize(container.clientWidth, container.clientHeight);
        this.renderer.setPixelRatio(window.devicePixelRatio);

        // 启用局部裁剪
        this.renderer.localClippingEnabled = true;
        this.renderer.clippingPlanes = [];

        container.appendChild(this.renderer.domElement);

        this.controls = new THREE.OrbitControls(this.camera, this.renderer.domElement);
        this.controls.enableDamping = true;
        this.controls.dampingFactor = 0.05;
        this.controls.minDistance = 1;
        this.controls.maxDistance = 500;

        this.scene.add(new THREE.AmbientLight(0x404040, 0.6));
        const dirLight = new THREE.DirectionalLight(0x2DD4BF, 0.8);
        dirLight.position.set(10, 10, 5);
        this.scene.add(dirLight);
        
        // ============ 自定义坐标轴辅助器，符合右手坐标系 ============
        // X轴(红色) - 指向用户(屏幕外)
        // Y轴(绿色) - 水平向右
        // Z轴(蓝色) - 竖直向上
        this.createCustomAxesHelper(10);

        const grid = new THREE.GridHelper(200, 20, 0x2DD4BF, 0x1E293B);
        grid.material.opacity = 0.3;
        grid.material.transparent = true;
        this.scene.add(grid);

        window.addEventListener('resize', () => this.onWindowResize());
        this.animate();
    }
    
    // ============ 创建自定义坐标轴辅助器 ============
createCustomAxesHelper(size) {
    const axesGroup = new THREE.Group();
    
    // X 轴 - 红色 - 水平向右（常规点云标准）
    const xGeometry = new THREE.BufferGeometry().setFromPoints([
        new THREE.Vector3(0, 0, 0),
        new THREE.Vector3(size, 0, 0)
    ]);
    const xMaterial = new THREE.LineBasicMaterial({ color: 0xff0000, linewidth: 2 });
    const xAxis = new THREE.Line(xGeometry, xMaterial);
    axesGroup.add(xAxis);
    
    // Y 轴 - 绿色 - 垂直向上（Three.js 标准）
    const yGeometry = new THREE.BufferGeometry().setFromPoints([
        new THREE.Vector3(0, 0, 0),
        new THREE.Vector3(0, size, 0)
    ]);
    const yMaterial = new THREE.LineBasicMaterial({ color: 0x00ff00, linewidth: 2 });
    const yAxis = new THREE.Line(yGeometry, yMaterial);
    axesGroup.add(yAxis);
    
    // Z 轴 - 蓝色 - 指向屏幕外（深度方向）
    const zGeometry = new THREE.BufferGeometry().setFromPoints([
        new THREE.Vector3(0, 0, 0),
        new THREE.Vector3(0, 0, size)
    ]);
    const zMaterial = new THREE.LineBasicMaterial({ color: 0x0000ff, linewidth: 2 });
    const zAxis = new THREE.Line(zGeometry, zMaterial);
    axesGroup.add(zAxis);
    
    // 添加轴标签
    this.addAxisLabel(axesGroup, 'X', size + 1, 0, 0, 0xff0000);
    this.addAxisLabel(axesGroup, 'Y', 0, size + 1, 0, 0x00ff00);
    this.addAxisLabel(axesGroup, 'Z', 0, 0, size + 1, 0x0000ff);
    
    this.scene.add(axesGroup);
}
    
    addAxisLabel(parent, text, x, y, z, color) {
    const canvas = document.createElement('canvas');
    canvas.width = 64;
    canvas.height = 64;
    const ctx = canvas.getContext('2d'); 
    ctx.fillStyle = '#' + color.toString(16).padStart(6, '0');
    ctx.font = 'bold 48px Arial';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(text, 32, 32);

    const texture = new THREE.CanvasTexture(canvas);
    const spriteMaterial = new THREE.SpriteMaterial({ map: texture });
    const sprite = new THREE.Sprite(spriteMaterial); 
    sprite.position.set(x, y, z);
    
    sprite.scale.set(0.25, 0.25, 0.25); 
    
    parent.add(sprite);
}

    initDatabaseConnection() {
        setTimeout(() => this.loadDataFromDatabase(), 1000);
    }

    async loadDataFromDatabase() {
        try {
            this.showLoading(true, '正在连接数据库...');
            const res = await fetch(`${this.apiBaseUrl}/api/lakes`);
            this.lakeData = await res.json();
            console.log('📊 湖泊数据:', this.lakeData);

            for (const lakeName in this.lakeData) {
                if (this.lakeData[lakeName].sensorRanges) {
                    const ranges = this.lakeData[lakeName].sensorRanges;
                    this.sensorRanges[lakeName] = {
                        depth: { min: ranges.minDepth ?? 45, max: ranges.maxDepth ?? 120 },
                        temperature: { min: ranges.minTemp ?? 0, max: ranges.maxTemp ?? 6 },
                        dissolved_oxygen: { min: ranges.minDO ?? 10, max: ranges.maxDO ?? 14 },
                        ph: { min: ranges.minPH ?? 7.5, max: ranges.maxPH ?? 8.5 },
                        conductivity: { min: ranges.minCond ?? 50, max: ranges.maxCond ?? 150 },
                        turbidity: { min: ranges.minTurb ?? 0.1, max: ranges.maxTurb ?? 1.0 }
                    };
                    console.log(`🎯 ${lakeName} 传感器范围:`, this.sensorRanges[lakeName]);
                }
            }

            this.showNotification('数据库连接成功', 'success');
            this.loadTaskList();
            
            // 动态选择第一个湖泊
            const lakeNames = Object.keys(this.lakeData);
            if (lakeNames.length > 0) {
                setTimeout(() => this.selectLake(lakeNames[0]), 500);
            }
        } catch (e) {
            console.error(e);
            this.showNotification('数据库连接失败', 'error');
        } finally {
            this.showLoading(false);
        }
    }

    loadTaskList() {
        const taskListEl = document.getElementById('taskList');
        taskListEl.innerHTML = '';
        
        // 动态获取所有 task_id（支持数字和字符串）
        const lakeNames = Object.keys(this.lakeData);
        
        if (lakeNames.length === 0) {
            taskListEl.innerHTML = '<div class="text-gray-400 text-sm p-4">暂无探测任务</div>';
            return;
        }
        
        lakeNames.forEach((lakeName, index) => {
            const lakeData = this.lakeData[lakeName];
            if (!lakeData) return;

            const taskEl = document.createElement('div');
            taskEl.className = 'task-item p-3 rounded-lg glass-panel-light';
            taskEl.dataset.lakeName = lakeName;

            const status = 'active';
            const statusClass = `status-${status}`;

            taskEl.innerHTML = `
                <div class="flex items-center justify-between mb-2">
                    <h4 class="text-sm font-medium text-gray-200">${lakeName}</h4>
                    <span class="text-xs px-2 py-1 rounded ${statusClass}">
                        ${status === 'active' ? '进行中' : status === 'completed' ? '已完成' : '待执行'}
                    </span>
                </div>
                <div class="text-xs text-gray-400 space-y-1">
                    <div>📍 ${this.getLakeLocation(lakeName)}</div>
                    <div>&emsp;${lakeData.pointCount.toLocaleString()} 点云</div>
                    <div>&emsp;${lakeData.traceCount.toLocaleString()} 轨迹点</div>
                </div>
            `;
            taskEl.addEventListener('click', () => this.selectLake(lakeName));
            taskListEl.appendChild(taskEl);
        });
    }

    // 高亮指定的传感器卡片
    highlightSensorCard(mode) {
        // 移除所有卡片的 active 样式
        document.querySelectorAll('.sensor-card').forEach(c => {
            c.classList.remove('ring-2', 'ring-teal-400', 'shadow-[0_0_15px_rgba(45,212,191,0.5)]', 'bg-teal-900/30');
            c.classList.add('border-gray-700/50', 'bg-gray-800/30');
        });
        // 为指定卡片添加 active 样式
        const cardId = mode === 'dissolved_oxygen' ? 'dissolvedOxygenCard' : `${mode}Card`;
        const card = document.getElementById(cardId);
        if (card) {
            card.classList.add('ring-2', 'ring-teal-400', 'shadow-[0_0_15px_rgba(45,212,191,0.5)]', 'bg-teal-900/30');
            card.classList.remove('border-gray-700/50', 'bg-gray-800/30');
        }
    }

    // 高亮指定的时间范围按钮
    highlightTimeRange(range) {
        document.querySelectorAll('.time-range-btn').forEach(btn => {
            btn.classList.remove('bg-teal-600', 'text-white', 'shadow-md');
            btn.classList.add('bg-teal-900', 'text-teal-400');
        });
        const activeBtn = document.getElementById(`btn-${range}`);
        if (activeBtn) {
            activeBtn.classList.remove('bg-teal-900', 'text-teal-400');
            activeBtn.classList.add('bg-teal-600', 'text-white', 'shadow-md');
        }
        this.currentTimeRange = range;
    }

    selectLake(lakeName) {
        document.querySelectorAll('.task-item').forEach(el => el.classList.remove('active'));
        const el = document.querySelector(`[data-lake-name="${lakeName}"]`);
        if (el) el.classList.add('active');
        this.currentLake = lakeName;
        
        // 时间范围默认重置为1d，传感器高亮由 loadLakeData 动态决定
        this.highlightTimeRange('1d');
        
        this.loadLakeData(lakeName);
    }

    // 获取第一个有有效数据的传感器字段（按优先级顺序）
    getFirstValidSensorMode() {
        const priority = ['depth', 'temperature', 'dissolved_oxygen', 'ph', 'conductivity', 'turbidity'];
        for (const field of priority) {
            const value = this.sensorData?.[field];
            if (value != null && value !== '' && !isNaN(Number(value))) {
                return field;
            }
        }
        return 'depth'; // 保底
    }

    async loadLakeData(lakeName) {
        const lakeData = this.lakeData[lakeName];
        if (!lakeData) return;

        // 确保复制完整数据
        this.sensorData = { ...lakeData.sensorData };
        console.log('📊 传感器数据已加载:', this.sensorData);
            
        // 检查是否有有效的传感器数据
        this.hasValidSensorData = this.checkValidSensorData();
        
        this.updateSensorDisplay();

        // 动态选择默认可视化模式：优先深度，无深度则按顺序递补
        const defaultMode = this.getFirstValidSensorMode();
        this.currentVisualizationMode = defaultMode;
        this.highlightSensorCard(defaultMode);

        if (this.sensorRanges[lakeName]) {
            this.currentLakeRanges = { ...this.sensorRanges[lakeName] };
            console.log(`📍 切换到 ${lakeName}，使用范围:`, this.currentLakeRanges);
        } else {
            console.warn(`⚠️ 未找到 ${lakeName} 的传感器范围，使用默认值`);
        }

        // 重置动态范围
        this.dynamicRanges = {};

        this.colorStats = { minNorm: 1, maxNorm: 0, avgNorm: 0, normSum: 0, normCount: 0 };
        this.colorCache.clear();

        this.clearPointCloudBatches();
        if (this.tracePoints) {
            this.scene.remove(this.tracePoints);
            this.tracePoints.geometry.dispose();
            this.tracePoints.material.dispose();
            this.tracePoints = null;
        }

        await this.generatePointCloud();
        await this.generateTrace();

        // 时间分布图也使用动态选择的默认模式
        this.updateTimeChart(defaultMode, '1d');
        this.updateTaskDetails(lakeName);

        this.updateLegend();

        this.dataLoaded = true;

        const noDataMsg = document.getElementById('noDataMessage');
        if (noDataMsg) noDataMsg.style.display = 'none';

        const currentView = document.getElementById('currentView');
        if (currentView) {
            const modeName = this.getVisualizationModeName(defaultMode);
            currentView.textContent = `${lakeName} - ${modeName} - 加载中...`;
        }
        // 居中操作已移至 loadNextPointCloudBatch 第一批加载完成后自动执行
    }

    // 检查是否有有效的传感器数据
    checkValidSensorData() {
        const fields = ['depth', 'temperature', 'dissolved_oxygen', 'ph', 'conductivity', 'turbidity'];
        return fields.some(field => {
            const value = this.sensorData?.[field];
            return value != null && value !== '' && !isNaN(Number(value));
        });
    }

    updateSensorDisplay() {
        const fields = ['depth', 'temperature', 'dissolved_oxygen', 'ph', 'conductivity', 'turbidity'];
        const units = {
            depth: 'm', temperature: '°C', dissolved_oxygen: 'mg/L',
            ph: '', conductivity: 'μS/cm', turbidity: 'NTU'
        };
        const labels = {
            depth: '深度', temperature: '温度', dissolved_oxygen: '溶解氧',
            ph: 'pH', conductivity: '电导率', turbidity: '浊度'
        };

        const container = document.getElementById('sensorCardsContainer');
        if (!container) return;

        // 判断哪些字段有有效数据
        const validFields = fields.filter(field => {
            const value = this.sensorData?.[field];
            return value != null && value !== '' && !isNaN(Number(value));
        });

        const hintEl = document.getElementById('sensorCardHint');
        if (hintEl) {
            hintEl.textContent = validFields.length > 0 ? `${validFields.length}项可用` : '暂无传感器数据';
            hintEl.className = validFields.length > 0 ? 'text-[10px] text-teal-500' : 'text-[10px] text-red-400';
        }

        // 清空容器并重新生成卡片
        container.innerHTML = '';

        if (validFields.length === 0) {
            container.innerHTML = `
                <div class="col-span-3 text-center py-3 text-xs text-gray-500 bg-gray-800/20 rounded border border-gray-700/30">
                    当前任务暂无传感器数据
                </div>
            `;
            return;
        }

        // 根据有效字段数决定列数
        const colClass = validFields.length <= 2 ? 'grid-cols-' + validFields.length : 
                         validFields.length === 3 ? 'grid-cols-3' : 
                         validFields.length <= 4 ? 'grid-cols-2' : 'grid-cols-3';
        container.className = `grid gap-2 ${colClass}`;

        validFields.forEach(field => {
            const val = Number(this.sensorData[field]).toFixed(1);
            const unit = units[field];
            const label = labels[field];
            const cardId = field === 'dissolved_oxygen' ? 'dissolvedOxygenCard' : `${field}Card`;
            const valueId = field === 'dissolved_oxygen' ? 'dissolvedOxygenValue' : `${field}Value`;

            const card = document.createElement('div');
            card.className = 'sensor-card rounded p-2 border border-gray-700/50 bg-gray-800/30';
            card.id = cardId;
            card.title = `点击显示${label}分布`;
            card.innerHTML = `
                <div class="text-xs text-gray-400">${label}</div>
                <div class="text-base font-bold text-teal-400" id="${valueId}">${val}${unit}</div>
                <div class="text-xs text-gray-500">${unit}</div>
            `;
            card.addEventListener('click', () => this.switchVisualizationMode(field));
            container.appendChild(card);
        });
    }

    async generatePointCloud(lakeName = this.currentLake, reset = true) {
        if (!lakeName || !this.dataLoaded) return;

        if (reset) {
            this.clearPointCloudBatches();
            this.currentBatchPage = 1;
            this.renderStats.renderedPoints = 0;
            this.dynamicRanges = {};
        }

        this.currentLake = lakeName;
        const lakeData = this.lakeData[lakeName];

        const lodFactor = { 1: 1, 2: 0.5, 3: 0.25, 4: 0.1 }[this.currentLOD] || 1;
        const maxRenderPoints = Math.min(Math.floor(lakeData.pointCount * lodFactor), 1000000);

        console.log(`🎯 点云：总数 ${lakeData.pointCount.toLocaleString()}，LOD${this.currentLOD}，目标 ${maxRenderPoints.toLocaleString()} 点`);

        await this.loadNextPointCloudBatch(maxRenderPoints);
        this.updatePointCloudInfo();
        this.updateLegend();
    }

    async loadNextPointCloudBatch(maxPoints) {
        if (this.isLoadingMore || this.renderStats.renderedPoints >= maxPoints) return;
        this.isLoadingMore = true;
        const startTime = performance.now();

        try {
            const params = new URLSearchParams({
                lake: this.currentLake,
                page: this.currentBatchPage,
                limit: this.batchSize,
                lod: this.currentLOD,
                mode: this.currentVisualizationMode
            });

            console.log(`📡 请求点云：${params.toString()}`);

            const controller = new AbortController();
            const timeoutId = setTimeout(() => controller.abort(), 30000);

            const response = await fetch(`${this.apiBaseUrl}/api/points?${params}`, {
                signal: controller.signal
            });

            clearTimeout(timeoutId);

            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }

            const result = await response.json();
            console.log(`✅ 批次 ${this.currentBatchPage} 返回 ${result.points?.length || 0} 条数据`);

            this.loadRetryCount = 0;

            if (result.points?.length > 0) {
                // 更新点云边界
                this.updatePointCloudBounds(result.points);
                
                // 同步后端实际关联阈值到前端，确保颜色衰减与后端关联逻辑一致
                if (result.meta?.adaptiveThreshold) {
                    this.adaptiveDistanceThreshold.currentThreshold = result.meta.adaptiveThreshold;
                }
                
                // 自适应调整点云关联限制
                this.adaptiveAdjustPointLimits(result.points.length);

                if (this.useAutoContrast) {
                    this.updateDynamicRanges(result.points);
                }

                const wasFirstBatch = this.pointCloudBatches.length === 0;
                await this.renderPointCloudBatch(result.points);
                this.renderStats.renderedPoints += result.points.length;
                this.currentBatchPage++;

                this.updatePointCloudInfo();
                
                // 第一批加载完成后自动居中，避免点云偏向屏幕一侧
                if (wasFirstBatch) {
                    setTimeout(() => this.centerModel(), 100);
                }

                const currentView = document.getElementById('currentView');
                if (currentView) {
                    const rate = result.meta.associationRate || 0;
                    const contrastMode = this.useAutoContrast ? '自动对比度' : '全局范围';
                    currentView.textContent = `${this.currentLake} • 已加载 ${this.renderStats.renderedPoints.toLocaleString()} 点 • 关联率 ${rate}% • ${contrastMode}`;
                }

                if (result.meta.hasMore && this.renderStats.renderedPoints < maxPoints && this.pointCloudBatches.length < this.maxBatches) {
                    if (window.requestIdleCallback) {
                        requestIdleCallback(() => this.loadNextPointCloudBatch(maxPoints), { timeout: 100 });
                    } else {
                        setTimeout(() => this.loadNextPointCloudBatch(maxPoints), 50);
                    }
                } else {
                    console.log(`✅ 点云加载完成：共 ${this.renderStats.renderedPoints.toLocaleString()} 点`);
                }
            }

            console.log(`⏱️ 批次 ${this.currentBatchPage - 1} 耗时 ${performance.now() - startTime}ms`);
        } catch (error) {
            console.error('❌ 加载点云批次失败:', error);

            if (this.loadRetryCount < this.maxRetries) {
                this.loadRetryCount++;
                console.log(`🔄 重试 ${this.loadRetryCount}/${this.maxRetries}`);
                setTimeout(() => this.loadNextPointCloudBatch(maxPoints), 1000 * this.loadRetryCount);
                return;
            }

            this.showNotification('点云加载失败：' + error.message, 'error');
        } finally {
            this.isLoadingMore = false;
            this.renderStats.lastLoadTime = performance.now() - startTime;
        }
    }
    
    // ============ 更新点云边界 ============
updatePointCloudBounds(points) {
    points.forEach(p => {
        // 使用与 renderPointCloudBatch 一致的映射
        const mappedX = p.y;        // Three.X = 数据库.Y
        const mappedY = p.z;        // Three.Y = 数据库.Z
        const mappedZ = -p.x;       // Three.Z = -数据库.X
        
        this.pointCloudBounds.minX = Math.min(this.pointCloudBounds.minX, mappedX);
        this.pointCloudBounds.maxX = Math.max(this.pointCloudBounds.maxX, mappedX);
        this.pointCloudBounds.minY = Math.min(this.pointCloudBounds.minY, mappedY);
        this.pointCloudBounds.maxY = Math.max(this.pointCloudBounds.maxY, mappedY);
        this.pointCloudBounds.minZ = Math.min(this.pointCloudBounds.minZ, mappedZ);
        this.pointCloudBounds.maxZ = Math.max(this.pointCloudBounds.maxZ, mappedZ);
    });
    
    // 更新切面滑块范围
    this.updateClipSliderRange();
}
    
    // ============ 更新切面滑块范围 ============
    updateClipSliderRange() {
        const slider = document.getElementById('clipPositionSlider');
        if (!slider) return;
        
        let min, max;
        switch(this.currentClipAxis) {
            case 'X':
                min = this.pointCloudBounds.minX;
                max = this.pointCloudBounds.maxX;
                break;
            case 'Y':
                min = this.pointCloudBounds.minY;
                max = this.pointCloudBounds.maxY;
                break;
            case 'Z':
                min = this.pointCloudBounds.minZ;
                max = this.pointCloudBounds.maxZ;
                break;
            default:
                min = -100; max = 100;
        }
        
        // 添加一些边距
        const range = max - min;
        min -= range * 0.1;
        max += range * 0.1;
        
        slider.min = min.toFixed(1);
        slider.max = max.toFixed(1);
        slider.value = ((parseFloat(min) + parseFloat(max)) / 2).toFixed(1);
        this.currentClipPosition = parseFloat(slider.value);
        
        const valueEl = document.getElementById('clipPositionValue');
        if (valueEl) valueEl.textContent = this.currentClipPosition.toFixed(1);
    }
    
    // ============ 自适应调整点云关联限制 ============
    adaptiveAdjustPointLimits(batchSize) {
        // 根据批次大小和性能动态调整
        const loadTime = this.renderStats.lastLoadTime;
        
        if (loadTime < 100) {
            // 加载快，可以增加限制
            this.adaptivePointLimit.currentLimit = Math.min(
                this.adaptivePointLimit.currentLimit * 1.2,
                this.adaptivePointLimit.maxLimit
            );
        } else if (loadTime > 500) {
            // 加载慢，减少限制
            this.adaptivePointLimit.currentLimit = Math.max(
                this.adaptivePointLimit.currentLimit * 0.8,
                this.adaptivePointLimit.minLimit
            );
        }
        
        console.log(`📊 自适应点云限制: ${Math.round(this.adaptivePointLimit.currentLimit)}, 加载时间: ${loadTime.toFixed(1)}ms`);
    }

    // 动态更新数据范围（自动对比度模式）
    updateDynamicRanges(points) {
        const mode = this.currentVisualizationMode;
        if (!this.dynamicRanges[mode]) {
            this.dynamicRanges[mode] = { min: Infinity, max: -Infinity };
        }

        points.forEach(p => {
            if (p.sensorData && p.sensorData[mode] != null) {
                const val = parseFloat(p.sensorData[mode]);
                if (val < this.dynamicRanges[mode].min) this.dynamicRanges[mode].min = val;
                if (val > this.dynamicRanges[mode].max) this.dynamicRanges[mode].max = val;
            }
        });

        if (this.dynamicRanges[mode].min !== Infinity && this.dynamicRanges[mode].max !== -Infinity) {
            const range = this.dynamicRanges[mode].max - this.dynamicRanges[mode].min;
            const margin = range * 0.05;
            this.currentLakeRanges[mode] = {
                min: this.dynamicRanges[mode].min - margin,
                max: this.dynamicRanges[mode].max + margin
            };
        }
    }

    // ============ renderPointCloudBatch 方法中的坐标映射 ============
async renderPointCloudBatch(points) {
    const geometry = new THREE.BufferGeometry();
    const positions = new Float32Array(points.length * 3);
    const colors = new Float32Array(points.length * 3);

    for (let i = 0; i < points.length; i++) {
        const i3 = i * 3;
        const p = points[i];

        // ============ 标准点云坐标映射 ============
        // 数据库坐标：X(东西), Y(南北), Z(深度/高度)
        // Three.js 坐标：X(右), Y(上), Z(屏幕外)
        positions[i3] = p.x;       // X = 数据库 X（东西方向）
        positions[i3 + 1] = p.z;   // Y = 数据库 Z（垂直方向）
        positions[i3 + 2] = p.y;   // Z = 数据库 Y（前后深度）

        this.setPointColorBySensorData(colors, i3, p.sensorData, p.nearestTraceDistance);
    }

        geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
        geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));
        geometry.computeBoundingSphere();

        const material = new THREE.PointsMaterial({
            size: this.getPointSize(),
            vertexColors: true,
            transparent: true,
            opacity: 0.95,
            sizeAttenuation: true,
            depthTest: true,
            depthWrite: false,
            clippingPlanes: this.clippingEnabled ? this.clippingPlanes : [],
            clipShadows: this.clippingEnabled
        });

        const batch = new THREE.Points(geometry, material);
        batch.frustumCulled = true;
        batch.userData = { batchIndex: this.pointCloudBatches.length, pointCount: points.length };

        this.scene.add(batch);
        this.pointCloudBatches.push(batch);

        if (this.pointCloudBatches.length > this.maxBatches) {
            const old = this.pointCloudBatches.shift();
            this.scene.remove(old);
            old.geometry.dispose();
            old.material.dispose();
        }
    }

    setPointColorBySensorData(colors, i3, sensorData, distance = Infinity) {
        const mode = this.currentVisualizationMode;

        if (!sensorData || sensorData[mode] == null) {
            colors[i3] = 1.0;
            colors[i3 + 1] = 1.0;
            colors[i3 + 2] = 1.0;
            return;
        }

        const value = parseFloat(sensorData[mode]);
        const range = this.currentLakeRanges[mode];

        const cacheKey = `${mode}_${value.toFixed(2)}`;
        if (this.colorCache.has(cacheKey)) {
            const cached = this.colorCache.get(cacheKey);
            colors[i3] = cached[0];
            colors[i3 + 1] = cached[1];
            colors[i3 + 2] = cached[2];
            return;
        }

        let norm = 0.5;
        if (range && range.max !== null && range.min !== null && range.max > range.min) {
            norm = (value - range.min) / (range.max - range.min);
            norm = Math.max(0, Math.min(1, norm));

            if (this.useAutoContrast) {
                norm = Math.pow(norm, 1 / this.contrastGamma);
            }

            this.colorStats.minNorm = Math.min(this.colorStats.minNorm, norm);
            this.colorStats.maxNorm = Math.max(this.colorStats.maxNorm, norm);
            this.colorStats.normSum += norm;
            this.colorStats.normCount++;
        }

        const color = this.getSmoothColor(mode, norm);
        // ============ 使用自适应距离阈值 ============
        // 双阈值策略：distanceFactor 保底 0.55，确保远距离点云仍保留明显颜色
        // 离轨迹越近越鲜艳（1.0），越远越淡（最低 0.55），始终能看出传感器颜色
        const rawFactor = Math.max(0, 1 - (distance / this.adaptiveDistanceThreshold.currentThreshold));
        const distanceFactor = 0.55 + rawFactor * 0.45; // 范围 [0.55, 1.0]

        colors[i3] = color[0] * distanceFactor + 0.35 * (1 - distanceFactor);
        colors[i3 + 1] = color[1] * distanceFactor + 0.38 * (1 - distanceFactor);
        colors[i3 + 2] = color[2] * distanceFactor + 0.42 * (1 - distanceFactor);

        if (this.colorCache.size < this.maxColorCacheSize) {
            this.colorCache.set(cacheKey, [colors[i3], colors[i3 + 1], colors[i3 + 2]]);
        }
    }

    getSmoothColor(mode, norm) {
        switch (mode) {
            case 'depth':
                return this.smoothGradient(norm, [0, 0.5, 1], [0, 1, 1], [1, 1, 0], [1, 0, 0]);
            case 'temperature':
                return this.smoothGradient(norm, [0, 0, 1], [0, 1, 1], [1, 1, 0], [1, 0, 0]);
            case 'dissolved_oxygen':
                return this.smoothGradient(norm, [0.6, 0, 0.8], [0, 0, 1], [0, 1, 1], [0, 1, 0]);
            case 'ph':
                return this.smoothGradient(norm, [1, 0, 0], [1, 1, 0], [0, 1, 0], [0, 0, 1]);
            case 'conductivity':
                return this.smoothGradient(norm, [0, 1, 0], [1, 1, 0], [1, 0.5, 0], [1, 0, 0]);
            case 'turbidity':
                return this.smoothGradient(norm, [0, 1, 1], [0, 1, 0], [1, 1, 0], [1, 0, 0]);
            default:
                return [0.5, 0.5, 0.5];
        }
    }

    smoothGradient(norm, ...colors) {
        const numColors = colors.length;
        if (numColors === 0) return [0.5, 0.5, 0.5];
        if (numColors === 1) return colors[0];

        const segment = Math.min(norm * (numColors - 1), numColors - 1.0001);
        const index = Math.floor(segment);
        const t = segment - index;
        const smoothT = t * t * (3 - 2 * t);

        const c1 = colors[index];
        const c2 = colors[Math.min(index + 1, numColors - 1)];

        return [
            c1[0] + (c2[0] - c1[0]) * smoothT,
            c1[1] + (c2[1] - c1[1]) * smoothT,
            c1[2] + (c2[2] - c1[2]) * smoothT
        ];
    }

    getPointSize() {
        const base = 0.08;
        const lodFactor = { 1: 1, 2: 1.3, 3: 1.6, 4: 2 }[this.currentLOD] || 1;
        
        if (!this.hasValidSensorData) {
            // 无传感器数据时，点云渲染更小，放大观察时更清晰
            return base * lodFactor * 0.25;
        }
        
        return base * lodFactor;
    }

    clearPointCloudBatches() {
        this.pointCloudBatches.forEach(batch => {
            this.scene.remove(batch);
            batch.geometry.dispose();
            batch.material.dispose();
        });
        this.pointCloudBatches = [];
        this.renderStats.renderedPoints = 0;
        this.currentBatchPage = 1;
        this.colorCache.clear();
        
        if (this.traceMarkers) {
            this.traceMarkers.forEach(m => {
                this.scene.remove(m);
                m.geometry.dispose();
                m.material.dispose();
            });
            this.traceMarkers = [];
        }
        this.tracePointsData = [];
        this.hoveredTracePoint = null;
        this.hideTraceTooltip();
        
        // 重置点云边界
        this.pointCloudBounds = {
    minX: -500, maxX: 500,
    minY: -500, maxY: 500, 
    minZ: -500, maxZ: 500
};
    }

    // ============ 机器人轨迹为连续线，使用发光半透明效果 ============
    async generateTrace() {
        if (this.tracePoints) {
            this.scene.remove(this.tracePoints); 
            this.tracePoints.geometry.dispose();
            this.tracePoints.material.dispose();
            this.tracePoints = null;
        }
        
        if (this.traceLineForRaycast) {
            this.scene.remove(this.traceLineForRaycast);
            this.traceLineForRaycast.geometry.dispose();
            this.traceLineForRaycast.material.dispose();
            this.traceLineForRaycast = null;
        }
        
        if (this.traceMarkers && this.traceMarkers.length > 0) {
            this.traceMarkers.forEach(m => {
                this.scene.remove(m);
                m.geometry.dispose();
                m.material.dispose();
            });
            this.traceMarkers = [];
        }
        
        this.tracePointsData = [];
        
        if (!this.currentLake || !this.dataLoaded) return;

        const lakeData = this.lakeData[this.currentLake];
        const MAX_TRACE = 20000;
        const traceCount = Math.min(lakeData.traceCount, MAX_TRACE);
        const allPos = [];
        const BATCH = 2000;

        for (let page = 1; page <= Math.ceil(traceCount / BATCH); page++) {
        try {
            const res = await fetch(`${this.apiBaseUrl}/api/trajectory?lake=${encodeURIComponent(this.currentLake)}&page=${page}&limit=${BATCH}`);
            const result = await res.json();
            if (result.trajectory?.length > 0) {
                result.trajectory.forEach(p => {
                    // ============ 修改：坐标映射与点云一致 ============
                    allPos.push(p.position.x, p.position.z, p.position.y);
                    this.tracePointsData.push({
                        position: new THREE.Vector3(p.position.x, p.position.z, p.position.y),
                        originalPosition: { x: p.position.x, y: p.position.y, z: p.position.z },
                        sensorData: p.sensorData || null,
                        timestamp: p.time,
                        speed: p.speed,
                        heading: p.heading,
                        battery: p.battery,
                        status: p.status
                    });
                });
            }
            if (!result.meta.hasMore) break;
        } catch (e) {
            console.error('轨迹加载失败:', e);
            break;
        }
    }
        if (allPos.length > 0) {
            const geometry = new THREE.BufferGeometry();
            geometry.setAttribute('position', new THREE.Float32BufferAttribute(allPos, 3));

            // ============ 轨迹线使用发光半透明效果 ============
            const material = new THREE.LineBasicMaterial({
                color: 0x00ffff,      // 青色
                transparent: true,
                opacity: 0.35,        // 较低透明度，不遮挡点云
                linewidth: 2
            });

            this.tracePoints = new THREE.Line(geometry, material);
            this.scene.add(this.tracePoints);
            
            // 创建用于射线检测的粗线（不可见）
            this.createRaycastTraceLine(allPos);
            
            // 创建轨迹点交互标记
            this.createTracePointMarkers();
        }
    }
    
    // ============ 创建用于射线检测的轨迹线 ============
    createRaycastTraceLine(positions) {
        // 使用多个小线段来近似轨迹，便于悬停检测
        const points = [];
        for (let i = 0; i < positions.length; i += 3) {
            points.push(new THREE.Vector3(positions[i], positions[i + 1], positions[i + 2]));
        }
        
        // 创建点序列用于插值
        this.tracePointsForInterpolation = points;
    }

    // ============ 创建轨迹点交互标记方法 ============
    createTracePointMarkers() {
        if (this.tracePointsData.length === 0) return;
        
        // 每隔一定距离创建可交互标记
        const step = Math.max(1, Math.floor(this.tracePointsData.length / 200));
        
        for (let i = 0; i < this.tracePointsData.length; i += step) {
            const point = this.tracePointsData[i];
            const geometry = new THREE.SphereGeometry(0.5, 8, 8);
            const material = new THREE.MeshBasicMaterial({
                color: 0x00ffff,
                transparent: true,
                opacity: 0.4,
                depthTest: false
            });
            
            const marker = new THREE.Mesh(geometry, material);
            marker.position.copy(point.position);
            marker.userData = { 
                traceIndex: i,
                traceData: point
            };
            
            this.scene.add(marker);
            this.traceMarkers.push(marker);
        }
        
        console.log(`✅ 创建 ${this.traceMarkers.length} 个轨迹交互标记点`);
    }

    getVisualizationModeName(mode) {
        return {
            depth: '深度分布',
            temperature: '温度分布',
            dissolved_oxygen: '溶解氧分布',
            ph: 'pH 分布',
            conductivity: '电导率分布',
            turbidity: '浊度分布'
        }[mode] || '未知';
    }

    switchVisualizationMode(mode) {
        if (!this.dataLoaded) {
            this.showNotification('请先连接数据库', 'error');
            return;
        }
        this.currentVisualizationMode = mode;

        this.highlightSensorCard(mode);

        if (this.pointCloudBatches.length > 0) {
            if (this.useAutoContrast) {
                this.dynamicRanges = {};
            }
            this.generatePointCloud(this.currentLake, true);
            this.generateTrace();
        }

        this.updateTimeChart(mode, this.currentTimeRange || '1d');
        if (this.currentLake) this.updateTaskDetails(this.currentLake);

        this.updateLegend();

        this.showNotification(`已切换到${this.getVisualizationModeName(mode)}`, 'success');
    }

    toggleAutoContrast() {
        this.useAutoContrast = !this.useAutoContrast;
        const btn = document.getElementById('autoContrastBtn');
        if (btn) {
            btn.style.transition = 'all 0.3s ease';

            if (this.useAutoContrast) {
                btn.classList.add('bg-teal-600', 'text-white', 'shadow-lg', 'shadow-teal-500/50');
                btn.classList.remove('bg-teal-900', 'text-teal-400');
                btn.innerHTML = '⚡ 自动对比度：<span class="font-bold">开</span>';
            } else {
                btn.classList.remove('bg-teal-600', 'text-white', 'shadow-lg', 'shadow-teal-500/50');
                btn.classList.add('bg-teal-900', 'text-teal-400');
                btn.innerHTML = '⚡ 自动对比度：<span class="font-bold">关</span>';
            }
        }

        if (this.useAutoContrast) {
            this.dynamicRanges = {};
            console.log('🔍 模式切换：自动对比度');
        } else {
            if (this.currentLake && this.sensorRanges[this.currentLake]) {
                this.currentLakeRanges = { ...this.sensorRanges[this.currentLake] };
            }
        }

        setTimeout(() => {
            this.showLoading(true, this.useAutoContrast ? '正在分析数据分布...' : '正在应用全局范围...');

            this.generatePointCloud(this.currentLake, true).then(() => {
                this.updateLegend();
                this.showLoading(false);
                this.showNotification(
                    this.useAutoContrast ? '✅ 自动对比度已启用' : '✅ 已恢复标准全局范围',
                    'success'
                );
            });
        }, 50);
    }

    async updateTimeChart(mode, range = '1d') {
        if (!this.timeChart || !this.currentLake) return;
        this.showLoading(true, '正在加载历史数据...');

        try {
            const res = await fetch(`${this.apiBaseUrl}/api/timeseries?lake=${encodeURIComponent(this.currentLake)}&mode=${mode}&range=${range}`);
            const result = await res.json();
            const data = result.data || result;

            const timeData = data.map(i => i.time);
            const valueData = data.map(i => parseFloat(i.value));

            const units = {
                'depth': 'm', 'temperature': '°C', 'dissolved_oxygen': 'mg/L',
                'ph': '', 'conductivity': 'μS/cm', 'turbidity': 'NTU'
            };

            const unit = units[mode] || '';

            const option = {
                grid: { left: 50, right: 40, top: 35, bottom: 60, containLabel: true },
                title: {
                    text: `${this.getVisualizationModeName(mode)}`,
                    left: 'center',
                    top: 8,
                    textStyle: {
                        color: '#2DD4BF',
                        fontSize: 13,
                        fontWeight: 'bold',
                        textShadowColor: 'rgba(45, 212, 191, 0.5)',
                        textShadowBlur: 4
                    }
                },
                dataZoom: [
                    { type: 'inside', start: 0, end: 100 },
                    {
                        type: 'slider', start: 0, end: 100, height: 20, bottom: 10,
                        borderColor: '#1E293B', fillerColor: 'rgba(45, 212, 191, 0.2)',
                        handleStyle: { color: '#2DD4BF' }, textStyle: { color: '#94A3B8' }
                    }
                ],
                xAxis: {
                    type: 'category',
                    data: timeData,
                    axisLine: { lineStyle: { color: '#64748B', width: 2 } },
                    axisLabel: { color: '#94A3B8', fontSize: 9 },
                    splitLine: { show: true, lineStyle: { color: '#1E293B', opacity: 0.3, type: 'dashed' } }
                },
                yAxis: {
                    type: 'value',
                    name: unit,
                    nameLocation: 'end',
                    nameGap: 5,
                    nameTextStyle: { color: '#2DD4BF', fontSize: 11, fontWeight: 'bold' },
                    axisLine: { show: true, lineStyle: { color: '#64748B', width: 2 } },
                    axisLabel: { color: '#94A3B8', fontSize: 10, margin: 8 },
                    splitLine: { lineStyle: { color: '#2DD4BF', opacity: 0.15, type: 'solid' } }
                },
                series: [{
                    data: valueData,
                    type: 'line',
                    smooth: true,
                    symbol: valueData.length > 1000 ? 'none' : 'circle',
                    symbolSize: 4,
                    sampling: valueData.length > 1000 ? 'lttb' : 'none',
                    lineStyle: { color: '#2DD4BF', width: 2 },
                    areaStyle: {
                        color: {
                            type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
                            colorStops: [
                                { offset: 0, color: 'rgba(45,212,191,0.25)' },
                                { offset: 1, color: 'rgba(45,212,191,0.02)' }
                            ]
                        }
                    }
                }],
                tooltip: {
                    trigger: 'axis',
                    backgroundColor: 'rgba(15,23,42,0.9)',
                    borderColor: '#2DD4BF',
                    textStyle: { color: '#94A3B8', fontSize: 11 },
                    formatter: (p) => `<div style="font-weight:bold">${p[0].name}</div><span style="color:#2DD4BF;font-weight:bold">${p[0].value} ${unit}</span>`
                }
            };
            this.timeChart.setOption(option, true);
        } catch (e) {
            console.error('更新时间图表失败:', e);
        } finally {
            this.showLoading(false);
        }
    }

    setTimeRange(range) {
        this.highlightTimeRange(range);
        this.updateTimeChart(this.currentVisualizationMode, range);
    }

    updateTaskDetails(lakeName) {
        const el = document.getElementById('taskDetails');
        const lakeData = this.lakeData[lakeName];
        if (!lakeData) {
            el.innerHTML = `<div class="text-sm text-gray-400 text-center py-8">请选择湖泊查看详情</div>`;
            return;
        }

        const status = 'active';
        const statusColor = 'text-green-400';

        const fmt = (v) => v != null ? Number(v).toFixed(1) : '--';
        const { depth, temperature, dissolved_oxygen, ph, conductivity, turbidity } = this.sensorData;

        el.innerHTML = `
            <div class="space-y-3">
                <div><h4 class="text-sm font-medium text-gray-200 mb-1">${lakeName}探测任务</h4><p class="text-xs text-gray-400">${this.getLakeDescription(lakeName)}</p></div>
                <div class="grid grid-cols-2 gap-2 text-xs">
                    <div><span class="text-gray-400">状态:</span><span class="ml-1 ${statusColor}">${status === 'active' ? '进行中' : status === 'completed' ? '已完成' : '待执行'}</span></div>
                    <div><span class="text-gray-400">机器人:</span><span class="ml-1 text-teal-400">在线</span></div>
                    <div><span class="text-gray-400">位置:</span><span class="ml-1 text-gray-200">${this.getLakeLocation(lakeName)}</span></div>
                    <div><span class="text-gray-400">点云数:</span><span class="ml-1 text-gray-200">${lakeData.pointCount.toLocaleString()}</span></div>
                </div>
                <div class="pt-2 border-t border-gray-700">
                    <div class="text-xs text-gray-400 mb-2">传感器状态</div>
                    <div class="grid grid-cols-2 gap-2 text-xs">
                        <div><span class="text-gray-400">深度:</span><span class="ml-1 ${depth != null ? 'text-green-400' : 'text-red-400'}">${fmt(depth)}m</span></div>
                        <div><span class="text-gray-400">温度:</span><span class="ml-1 ${temperature != null ? 'text-green-400' : 'text-red-400'}">${fmt(temperature)}°C</span></div>
                        <div><span class="text-gray-400">溶解氧:</span><span class="ml-1 ${dissolved_oxygen != null ? 'text-green-400' : 'text-red-400'}">${fmt(dissolved_oxygen)}mg/L</span></div>
                        <div><span class="text-gray-400">pH:</span><span class="ml-1 ${ph != null ? 'text-green-400' : 'text-red-400'}">${fmt(ph)}</span></div>
                        <div><span class="text-gray-400">电导率:</span><span class="ml-1 ${conductivity != null ? 'text-green-400' : 'text-red-400'}">${fmt(conductivity)}μS/cm</span></div>
                        <div><span class="text-gray-400">浊度:</span><span class="ml-1 ${turbidity != null ? 'text-green-400' : 'text-red-400'}">${fmt(turbidity)}NTU</span></div>
                    </div>
                </div>
                <div class="pt-2 border-t border-gray-700">
                    <div class="text-xs text-gray-400 mb-2">当前可视化</div>
                    <div class="text-xs text-teal-400">${this.getVisualizationModeName(this.currentVisualizationMode)}</div>
                    <div class="text-xs text-gray-500 mt-1">对比度模式：${this.useAutoContrast ? '自动' : '全局'}</div>
                    <div class="text-xs ${this.hasValidSensorData ? 'text-green-400' : 'text-yellow-400'} mt-1">
                        传感器数据：${this.hasValidSensorData ? '有效' : '无有效数据'}
                    </div>
                </div>
            </div>
        `;
    }

    getLakeDescription(name) {
        const descriptionMap = {
            '纳木错湖': '对纳木错湖水下地形进行全面测绘，获取高精度三维点云数据。',
            '青海湖': '监测青海湖浅水区域的水生生态系统和环境参数。',
            '普莫雍错': '探测冰川湖泊的水下结构和温度分层现象。'
        };
        return descriptionMap[name] || `任务区域 ${name} 的水下三维探测数据`;
    }

    getLakeLocation(name) {
        const locationMap = {
            '纳木错湖': '西藏自治区',
            '青海湖': '青海省',
            '普莫雍错': '西藏自治区'
        };
        return locationMap[name] || '任务区域';
    }

    updateLegend() {
        const el = document.getElementById('dataRangeLegend');
        if (!el) {
            console.warn('⚠️ dataRangeLegend 元素不存在');
            return;
        }

        const mode = this.currentVisualizationMode;

        el.style.display = 'block';
        document.getElementById('legendTitle').textContent = this.getVisualizationModeName(mode);
        const bar = document.getElementById('legendColorBar');

        const range = this.currentLakeRanges[mode];
        const min = (range?.min !== undefined && range?.min !== null) ? range.min : 0;
        const max = (range?.max !== undefined && range?.max !== null) ? range.max : 100;

        console.log(`📊 [图例更新] ${mode} 范围:${min} - ${max}`);

        const gradients = {
            depth: 'linear-gradient(to right,#0891B2,#22D3EE,#FDE047,#EF4444)',
            temperature: 'linear-gradient(to right,#3B82F6,#22D3EE,#FDE047,#EF4444)',
            dissolved_oxygen: 'linear-gradient(to right,#A855F7,#3B82F6,#22D3EE,#10B981)',
            ph: 'linear-gradient(to right,#EF4444,#FDE047,#10B981,#3B82F6)',
            conductivity: 'linear-gradient(to right,#10B981,#FDE047,#F97316,#EF4444)',
            turbidity: 'linear-gradient(to right,#22D3EE,#10B981,#FDE047,#F97316,#EF4444)'
        };

        const units = {
            depth: 'm', temperature: '°C', dissolved_oxygen: 'mg/L',
            ph: '', conductivity: 'μS/cm', turbidity: 'NTU'
        };

        bar.style.background = gradients[mode] || gradients.depth;
        document.getElementById('legendMin').textContent = `${Number(min).toFixed(2)}${units[mode]}`;
        document.getElementById('legendMax').textContent = `${Number(max).toFixed(2)}${units[mode]}`;

        const descEl = document.getElementById('legendDescription');
        if (descEl) {
            descEl.textContent = '';
            descEl.style.display = 'none';
        }

        const modeIndicator = document.getElementById('legendModeIndicator');
        if (modeIndicator) {
            modeIndicator.textContent = this.useAutoContrast ? '🔍 自动范围' : '📊 全局范围';
            modeIndicator.style.color = this.useAutoContrast ? '#22D3EE' : '#94A3B8';
        }
    }

    updatePointCloudInfo() {
        const lakeData = this.lakeData[this.currentLake];
        const lodFactor = { 1: 1, 2: 0.5, 3: 0.25, 4: 0.1 }[this.currentLOD] || 1;
        const target = Math.min(Math.floor(lakeData.pointCount * lodFactor), 1000000);

        const currentView = document.getElementById('currentView');
        if (currentView) {
            currentView.textContent = `${this.currentLake} • ${this.renderStats.renderedPoints.toLocaleString()}/${target.toLocaleString()} 点 • LOD${this.currentLOD} • ${this.renderStats.lastLoadTime.toFixed(1)}ms`;
        }
    }

    showDatabasePanel() {
        const panel = document.createElement('div');
        panel.className = 'fixed inset-0 bg-black bg-opacity-50 flex items-center justify-center z-50';
        panel.innerHTML = `
            <div class="glass-panel rounded-lg p-6 max-w-md w-full mx-4">
                <h3 class="text-lg font-semibold text-gray-200 mb-4">数据库连接信息</h3>
                <div class="space-y-3 text-sm text-gray-300">
                    <div><span class="text-gray-400">Host:</span> ${this.dbConfig.host}</div>
                    <div><span class="text-gray-400">Port:</span> ${this.dbConfig.port}</div>
                    <div><span class="text-gray-400">Database:</span> ${this.dbConfig.database}</div>
                    <div><span class="text-gray-400">User:</span> ${this.dbConfig.user}</div>
                    <div class="pt-2 border-t border-gray-700"><span class="text-gray-400">Status:</span> <span class="text-green-400">已连接</span></div>
                </div>
                <button onclick="this.parentElement.parentElement.remove()" class="btn-primary w-full mt-4 py-2 rounded text-sm">关闭</button>
            </div>
        `;
        document.body.appendChild(panel);
    }

resetView() {
    // ============ 标准视角 ============
    this.camera.position.set(100, 100, 100);
    this.controls.target.set(0, 0, 0);
    this.controls.update();
}

    togglePoints() {
        if (this.pointCloudBatches[0]) this.pointCloudBatches.forEach(b => b.visible = !b.visible);
        if (this.tracePoints) this.tracePoints.visible = !this.tracePoints.visible;
    }

    centerModel() {
    // 使用 pointCloudBounds（包含所有已加载批次的实际边界）来计算中心
    const bounds = this.pointCloudBounds;
    const center = new THREE.Vector3(
        (bounds.minX + bounds.maxX) / 2,
        (bounds.minY + bounds.maxY) / 2,
        (bounds.minZ + bounds.maxZ) / 2
    );
    const size = new THREE.Vector3(
        bounds.maxX - bounds.minX,
        bounds.maxY - bounds.minY,
        bounds.maxZ - bounds.minZ
    );

    this.pointCloudBatches.forEach(b => b.position.sub(center));
    if (this.tracePoints) this.tracePoints.position.sub(center);
    if (this.traceMarkers) {
        this.traceMarkers.forEach(m => m.position.sub(center));
    }

    const maxDim = Math.max(size.x, size.y, size.z);
    const fov = this.camera.fov * Math.PI / 180;
    let camZ = Math.abs(maxDim / 2 / Math.tan(fov / 2)) * 2;
    camZ = Math.max(camZ, 50);

    // 标准相机位置
    this.camera.position.set(camZ, camZ * 0.8, camZ * 0.8);
    this.controls.target.set(0, 0, 0);
    this.controls.update();
}
    
    // ============ 切面控制方法 ============

    // ============ setClippingPlane 方法中的法向量 ============
setClippingPlane(axis = 'Z', position = 0) {
    this.currentClipAxis = axis;
    this.currentClipPosition = position;
    
    let normal;
    // ============ 标准切面法向量 ============
switch(axis) {
    case 'X': 
        normal = new THREE.Vector3(1, 0, 0); 
        break;  // YZ 平面切面（垂直于 X 轴，左右切割）
    case 'Y': 
        normal = new THREE.Vector3(0, 1, 0); 
        break;  // XZ 平面切面（垂直于 Y 轴，水平切割）
    case 'Z': 
        normal = new THREE.Vector3(0, 0, 1); 
        break;  // XY 平面切面（垂直于 Z 轴，前后切割）
    default: 
        normal = new THREE.Vector3(0, 1, 0);
}
    this.clippingPlane = new THREE.Plane(normal, -position);
    this.clippingPlanes = [this.clippingPlane];
    
    this.renderer.clippingPlanes = this.clippingPlanes;
    
    this.pointCloudBatches.forEach(batch => {
        if (batch.material) {
            batch.material.clippingPlanes = this.clippingPlanes;
            batch.material.clipShadows = this.clippingEnabled;
            batch.material.needsUpdate = true;
        }
    });
    
    this.updateClippingPlaneVisual();
    
    const valueEl = document.getElementById('clipPositionValue');
    if (valueEl) valueEl.textContent = position.toFixed(1);
}
    
   updateClippingPlaneVisual() {
    if (this.clippingPlaneMesh) {
        this.scene.remove(this.clippingPlaneMesh);
        this.clippingPlaneMesh.geometry.dispose();
        this.clippingPlaneMesh.material.dispose();
        this.clippingPlaneMesh = null;
    }
    
    if (!this.clippingEnabled || !this.clippingPlane) return;
    
    const sizeX = this.pointCloudBounds.maxX - this.pointCloudBounds.minX;
    const sizeY = this.pointCloudBounds.maxY - this.pointCloudBounds.minY;
    const sizeZ = this.pointCloudBounds.maxZ - this.pointCloudBounds.minZ;
    
    let width, height;
    switch(this.currentClipAxis) {
        case 'X':  // YZ平面
            width = Math.max(sizeZ, 80);    // 
            height = Math.max(sizeY, 80);
            break;
        case 'Y':  // XZ平面
            width = Math.max(sizeX, 80);
            height = Math.max(sizeZ, 80);
            break;
        case 'Z':  // XY平面
            width = Math.max(sizeX, 80);
            height = Math.max(sizeY, 80);
            break;
        default: 
            width = 200; height = 200;
    }
    
    // 正常比例因子 + 小边距
    const baseFactor = 1.1;           // 
    const dynamicMargin = Math.max(30, Math.min(width, height) * 0.08);  // 
    
    const geometry = new THREE.PlaneGeometry(
        width * baseFactor + dynamicMargin,
        height * baseFactor + dynamicMargin
    );
    
    const material = new THREE.MeshBasicMaterial({
        color: 0x2DD4BF,
        transparent: true,
        opacity: 0.2,
        side: THREE.DoubleSide,
        depthWrite: false
    });
    
    this.clippingPlaneMesh = new THREE.Mesh(geometry, material);
    
    // 旋转逻辑保持不变
    switch(this.currentClipAxis) {
        case 'X':
            this.clippingPlaneMesh.rotation.y = Math.PI / 2;
            this.clippingPlaneMesh.position.x = this.currentClipPosition;
            break;
        case 'Y':
            this.clippingPlaneMesh.rotation.x = Math.PI / 2;
            this.clippingPlaneMesh.position.y = this.currentClipPosition;
            break;
        case 'Z':
            this.clippingPlaneMesh.position.z = this.currentClipPosition;
            break;
    }
    
    this.scene.add(this.clippingPlaneMesh);
}
    
    toggleClipping() {
        this.clippingEnabled = !this.clippingEnabled;
        
        const btn = document.getElementById('toggleClipBtn');
        const textEl = document.getElementById('toggleClipText');
        
        if (btn) {
            if (this.clippingEnabled) {
                btn.classList.add('clip-btn-active');
                btn.classList.remove('bg-gray-800', 'text-gray-300');
            } else {
                btn.classList.remove('clip-btn-active');
                btn.classList.add('bg-gray-800', 'text-gray-300');
            }
        }
        
        if (textEl) {
            textEl.textContent = this.clippingEnabled ? '切面已启用' : '启用切面';
        }
        
        if (this.clippingEnabled) {
            this.setClippingPlane(this.currentClipAxis, this.currentClipPosition);
            this.showNotification('✅ 切面分析已启用', 'success');
        } else {
            this.renderer.clippingPlanes = [];
            this.pointCloudBatches.forEach(batch => {
                if (batch.material) {
                    batch.material.clippingPlanes = [];
                    batch.material.needsUpdate = true;
                }
            });
            
            if (this.clippingPlaneMesh) {
                this.scene.remove(this.clippingPlaneMesh);
            }
            this.showNotification('切面分析已关闭', 'info');
        }
    }
    
    updateClipPosition(position) {
        this.currentClipPosition = position;
        if (this.clippingEnabled) {
            this.setClippingPlane(this.currentClipAxis, position);
        }
    }
    
    // ============ 轨迹点交互方法 - 支持线段悬停 ============
    onMouseMove(event) {
        const container = document.getElementById('threejs-container');
        if (!container) return;
        
        const rect = container.getBoundingClientRect();
        
        this.mouse.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
        this.mouse.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;
        
        this.raycaster.setFromCamera(this.mouse, this.camera);
        
        // 首先检测轨迹标记点
        if (this.traceMarkers && this.traceMarkers.length > 0) {
            const intersects = this.raycaster.intersectObjects(this.traceMarkers);
            
            if (intersects.length > 0) {
                const marker = intersects[0].object;
                const traceData = marker.userData.traceData;
                
                if (this.hoveredTracePoint !== marker) {
                    if (this.hoveredTracePoint) {
                        this.hoveredTracePoint.material.color.setHex(0x00ffff);
                        this.hoveredTracePoint.material.opacity = 0.4;
                        this.hoveredTracePoint.scale.set(1, 1, 1);
                    }
                    
                    marker.material.color.setHex(0xffdd00);
                    marker.material.opacity = 0.9;
                    marker.scale.set(1.5, 1.5, 1.5);
                    this.hoveredTracePoint = marker;
                    
                    this.showTraceTooltip(traceData, event.clientX, event.clientY);
                }
                return;
            }
        }
        
        // 如果没有命中标记点，检测轨迹线
        if (this.tracePoints) {
            const lineIntersects = this.raycaster.intersectObject(this.tracePoints);
            
            if (lineIntersects.length > 0) {
                const intersect = lineIntersects[0];
                const point = intersect.point;
                
                // 找到最近的轨迹点进行插值
                const interpolatedData = this.interpolateTraceData(point);
                if (interpolatedData) {
                    this.showTraceTooltip(interpolatedData, event.clientX, event.clientY);
                    return;
                }
            }
        }
        
        // 恢复之前点的颜色
        if (this.hoveredTracePoint) {
            this.hoveredTracePoint.material.color.setHex(0x00ffff);
            this.hoveredTracePoint.material.opacity = 0.4;
            this.hoveredTracePoint.scale.set(1, 1, 1);
            this.hoveredTracePoint = null;
        }
        
        this.hideTraceTooltip();
    }
    
    // ============ 插值计算轨迹点数据 ============
    interpolateTraceData(point) {
        if (this.tracePointsData.length < 2) return null;
        
        // 找到最近的两个轨迹点
        let nearestIndex = -1;
        let minDist = Infinity;
        
        for (let i = 0; i < this.tracePointsData.length; i++) {
            const dist = point.distanceTo(this.tracePointsData[i].position);
            if (dist < minDist) {
                minDist = dist;
                nearestIndex = i;
            }
        }
        
        if (nearestIndex === -1 || minDist > 5) return null; // 距离太远不显示
        
        const nearestPoint = this.tracePointsData[nearestIndex];
        
        // 找到相邻点进行插值
        let prevIndex = nearestIndex > 0 ? nearestIndex - 1 : nearestIndex;
        let nextIndex = nearestIndex < this.tracePointsData.length - 1 ? nearestIndex + 1 : nearestIndex;
        
        const prevPoint = this.tracePointsData[prevIndex];
        const nextPoint = this.tracePointsData[nextIndex];
        
        // 计算插值权重
        const distToPrev = point.distanceTo(prevPoint.position);
        const distToNext = point.distanceTo(nextPoint.position);
        const totalDist = distToPrev + distToNext;
        
        const weightPrev = totalDist > 0 ? distToNext / totalDist : 0.5;
        const weightNext = totalDist > 0 ? distToPrev / totalDist : 0.5;
        
        // 插值传感器数据
        const interpolateValue = (key) => {
            const prevVal = prevPoint.sensorData?.[key];
            const nextVal = nextPoint.sensorData?.[key];
            const nearestVal = nearestPoint.sensorData?.[key];
            
            if (nearestVal != null) return nearestVal;
            if (prevVal != null && nextVal != null) {
                return prevVal * weightPrev + nextVal * weightNext;
            }
            return prevVal ?? nextVal ?? null;
        };
        
        // 插值时间
        let interpolatedTime = nearestPoint.timestamp;
        if (!interpolatedTime && prevPoint.timestamp && nextPoint.timestamp) {
            // 取相邻时间的平均值
            const prevTime = new Date(prevPoint.timestamp).getTime();
            const nextTime = new Date(nextPoint.timestamp).getTime();
            const avgTime = new Date((prevTime + nextTime) / 2);
            interpolatedTime = avgTime.toLocaleString('zh-CN');
        }
        
        return {
            position: point,
            sensorData: {
                depth: interpolateValue('depth'),
                temperature: interpolateValue('temperature'),
                dissolved_oxygen: interpolateValue('dissolved_oxygen'),
                ph: interpolateValue('ph'),
                conductivity: interpolateValue('conductivity'),
                turbidity: interpolateValue('turbidity')
            },
            timestamp: interpolatedTime || '插值估算',
            speed: nearestPoint.speed,
            heading: nearestPoint.heading,
            isInterpolated: true
        };
    }
    
    showTraceTooltip(traceData, clientX, clientY) {
        const tooltip = document.getElementById('traceTooltip');
        if (!tooltip) return;
        
        const fmt = (v) => v != null ? Number(v).toFixed(2) : '--';
        const sensor = traceData.sensorData || {};
        
        document.getElementById('tooltipX').textContent = fmt(traceData.position.x);
        document.getElementById('tooltipY').textContent = fmt(traceData.position.y);
        document.getElementById('tooltipZ').textContent = fmt(traceData.position.z);
        
        document.getElementById('tooltipDepth').textContent = 
            sensor.depth != null ? fmt(sensor.depth) + ' m' : '--';
        document.getElementById('tooltipTemp').textContent = 
            sensor.temperature != null ? fmt(sensor.temperature) + ' °C' : '--';
        document.getElementById('tooltipDO').textContent = 
            sensor.dissolved_oxygen != null ? fmt(sensor.dissolved_oxygen) + ' mg/L' : '--';
        document.getElementById('tooltipPH').textContent = 
            sensor.ph != null ? fmt(sensor.ph) : '--';
        document.getElementById('tooltipCond').textContent = 
            sensor.conductivity != null ? fmt(sensor.conductivity) + ' μS/cm' : '--';
        document.getElementById('tooltipTurb').textContent = 
            sensor.turbidity != null ? fmt(sensor.turbidity) + ' NTU' : '--';
        
        // 添加插值标记
        const timePrefix = traceData.isInterpolated ? '~' : '';
        document.getElementById('tooltipTime').textContent = 
            timePrefix + (traceData.timestamp || '--');
        
        tooltip.classList.remove('hidden');
        
        const container = document.getElementById('threejs-container');
        const containerRect = container.getBoundingClientRect();
        
        let left = clientX - containerRect.left + 15;
        let top = clientY - containerRect.top + 15;
        
        if (left + 280 > containerRect.width) {
            left = containerRect.width - 295;
        }
        if (top + 300 > containerRect.height) {
            top = clientY - containerRect.top - 315;
        }
        
        tooltip.style.left = left + 'px';
        tooltip.style.top = top + 'px';
    }
    
    hideTraceTooltip() {
        const tooltip = document.getElementById('traceTooltip');
        if (tooltip) {
            tooltip.classList.add('hidden');
        }
    }

    showLoading(show, text = '正在加载...') {
        const overlay = document.getElementById('loadingOverlay');
        const loadingText = document.getElementById('loadingText');
        if (show) { loadingText.textContent = text; overlay.classList.remove('hidden'); }
        else { overlay.classList.add('hidden'); }
    }

    showNotification(message, type = 'info') {
        const notification = document.createElement('div');
        notification.className = `fixed top-24 right-6 z-50 px-4 py-3 rounded-lg text-white text-sm max-w-sm ${
            type === 'success' ? 'bg-green-600' : 
            type === 'error' ? 'bg-red-600' : 
            'bg-blue-600'
        }`;
        notification.innerHTML = message;
        
        document.body.appendChild(notification);
        
        if (typeof anime !== 'undefined') {
            anime({
                targets: notification,
                translateX: [300, 0],
                opacity: [0, 1],
                duration: 300,
                easing: 'easeOutQuad'
            });
        }
        
        setTimeout(() => {
            if (typeof anime !== 'undefined') {
                anime({
                    targets: notification,
                    translateX: [0, 300],
                    opacity: [1, 0],
                    duration: 300,
                    easing: 'easeInQuad',
                    complete: () => {
                        if (notification.parentNode) {
                            document.body.removeChild(notification);
                        }
                    }
                });
            } else {
                if (notification.parentNode) {
                    document.body.removeChild(notification);
                }
            }
        }, 3000);
    }

    initCharts() {
        const chartDom = document.getElementById('timeChart');
        if (chartDom) {
            this.timeChart = echarts.init(chartDom);
        }
    }

    initEventListeners() {
        // 传感器卡片点击事件
        document.getElementById('depthCard')?.addEventListener('click', () => this.switchVisualizationMode('depth'));
        document.getElementById('temperatureCard')?.addEventListener('click', () => this.switchVisualizationMode('temperature'));
        document.getElementById('dissolvedOxygenCard')?.addEventListener('click', () => this.switchVisualizationMode('dissolved_oxygen'));
        document.getElementById('phCard')?.addEventListener('click', () => this.switchVisualizationMode('ph'));
        document.getElementById('conductivityCard')?.addEventListener('click', () => this.switchVisualizationMode('conductivity'));
        document.getElementById('turbidityCard')?.addEventListener('click', () => this.switchVisualizationMode('turbidity'));

        // 控制按钮事件
        document.getElementById('resetViewBtn')?.addEventListener('click', () => this.resetView());
        document.getElementById('togglePointsBtn')?.addEventListener('click', () => this.togglePoints());
        document.getElementById('centerModelBtn')?.addEventListener('click', () => this.centerModel());
        document.getElementById('connectDbBtn')?.addEventListener('click', () => this.showDatabasePanel());
        document.getElementById('autoContrastBtn')?.addEventListener('click', () => this.toggleAutoContrast());
        
        // 导出按钮事件
        document.getElementById('exportShpBtn')?.addEventListener('click', () => this.exportPointCloud('shp'));
        
        // ============ 调试按钮显示弹窗 ============
        document.getElementById('debugBtn')?.addEventListener('click', () => {
            this.showDebugModal();
        });

        // 退出登录
        document.getElementById('logoutBtn')?.addEventListener('click', () => {
            sessionStorage.removeItem('user');
            window.location.href = 'index.html';
        });

        // 切面控制事件
document.getElementById('clipXYBtn')?.addEventListener('click', () => {
    // XY平面 = 垂直于Z轴的切面（前后切割）
    this.setClippingPlane('Z', this.currentClipPosition);
    ['clipXYBtn', 'clipXZBtn', 'clipYZBtn'].forEach(id => {
        document.getElementById(id)?.classList.remove('clip-btn-active');
    });
    document.getElementById('clipXYBtn')?.classList.add('clip-btn-active');
    this.updateClipSliderRange();
    this.showNotification('已切换到 XY 平面切面', 'success');
});

document.getElementById('clipXZBtn')?.addEventListener('click', () => {
    // XZ平面 = 垂直于Y轴的切面（水平切割/深度分层）
    this.setClippingPlane('Y', this.currentClipPosition);
    ['clipXYBtn', 'clipXZBtn', 'clipYZBtn'].forEach(id => {
        document.getElementById(id)?.classList.remove('clip-btn-active');
    });
    document.getElementById('clipXZBtn')?.classList.add('clip-btn-active');
    this.updateClipSliderRange();
    this.showNotification('已切换到 XZ 平面切面', 'success');
});

document.getElementById('clipYZBtn')?.addEventListener('click', () => {
    // YZ平面 = 垂直于X轴的切面（左右切割）
    this.setClippingPlane('X', this.currentClipPosition);
    ['clipXYBtn', 'clipXZBtn', 'clipYZBtn'].forEach(id => {
        document.getElementById(id)?.classList.remove('clip-btn-active');
    });
    document.getElementById('clipYZBtn')?.classList.add('clip-btn-active');
    this.updateClipSliderRange();
    this.showNotification('已切换到 YZ 平面切面', 'success');
});

        document.getElementById('clipPositionSlider')?.addEventListener('input', (e) => {
            const position = parseFloat(e.target.value);
            this.updateClipPosition(position);
        });

        document.getElementById('toggleClipBtn')?.addEventListener('click', () => {
            this.toggleClipping();
        });

        // 轨迹点交互事件
        const container = document.getElementById('threejs-container');
        if (container) {
            container.addEventListener('mousemove', (e) => this.onMouseMove(e));
            container.addEventListener('mouseleave', () => this.hideTraceTooltip());
        }

        window.addEventListener('resize', () => this.onWindowResize());
    }
    
    // ============ 显示调试信息弹窗 ============
    showDebugModal() {
        const validFields = [];
        const fields = ['depth', 'temperature', 'dissolved_oxygen', 'ph', 'conductivity', 'turbidity'];
        
        fields.forEach(field => {
            const value = this.sensorData?.[field];
            if (value !== null && value !== undefined && !isNaN(value)) {
                validFields.push(field);
            }
        });

        const modal = document.createElement('div');
        modal.className = 'fixed inset-0 bg-black bg-opacity-60 flex items-center justify-center z-50';
        modal.id = 'debugModal';
        modal.innerHTML = `
            <div class="glass-panel rounded-xl p-6 max-w-lg w-full mx-4 max-h-[80vh] overflow-y-auto">
                <div class="flex justify-between items-center mb-4">
                    <h3 class="text-lg font-semibold text-teal-400">调试信息</h3>
                    <button onclick="document.getElementById('debugModal').remove()" class="text-gray-400 hover:text-white">
                        <svg class="w-6 h-6" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12"/>
                        </svg>
                    </button>
                </div>
                
                <div class="space-y-4 text-sm">
                    <!-- 系统状态 -->
                    <div class="bg-gray-800/50 rounded-lg p-3">
                        <h4 class="text-teal-400 font-medium mb-2">系统状态</h4>
                        <div class="grid grid-cols-2 gap-2 text-gray-300">
                            <div>当前湖泊: <span class="text-white">${this.currentLake || '未选择'}</span></div>
                            <div>数据状态: <span class="${this.hasValidSensorData ? 'text-green-400' : 'text-yellow-400'}">${this.hasValidSensorData ? '有效' : '无有效数据'}</span></div>
                            <div>渲染点数: <span class="text-white">${this.renderStats.renderedPoints.toLocaleString()}</span></div>
                            <div>点云批次: <span class="text-white">${this.pointCloudBatches.length}</span></div>
                        </div>
                    </div>
                    
                    <!-- 传感器数据 -->
                    <div class="bg-gray-800/50 rounded-lg p-3">
                        <h4 class="text-teal-400 font-medium mb-2">传感器数据</h4>
                        <div class="grid grid-cols-2 gap-2 text-gray-300">
                            ${fields.map(f => {
                                const val = this.sensorData?.[f];
                                const displayVal = val != null ? Number(val).toFixed(2) : '--';
                                const isValid = val !== null && !isNaN(val);
                                return `<div class="${isValid ? 'text-green-400' : 'text-gray-500'}">${f}: ${displayVal}</div>`;
                            }).join('')}
                        </div>
                    </div>
                    
                    <!-- 渲染参数 -->
                    <div class="bg-gray-800/50 rounded-lg p-3">
                        <h4 class="text-teal-400 font-medium mb-2">渲染参数</h4>
                        <div class="grid grid-cols-2 gap-2 text-gray-300">
                            <div>点大小: <span class="text-white">${this.getPointSize().toFixed(3)}</span></div>
                            <div>LOD级别: <span class="text-white">${this.currentLOD}</span></div>
                            <div>对比度模式: <span class="text-white">${this.useAutoContrast ? '自动' : '全局'}</span></div>
                            <div>可视化模式: <span class="text-white">${this.currentVisualizationMode}</span></div>
                        </div>
                    </div>
                    
                    <!-- 点云边界 -->
                    <div class="bg-gray-800/50 rounded-lg p-3">
                        <h4 class="text-teal-400 font-medium mb-2">点云边界</h4>
                        <div class="grid grid-cols-3 gap-2 text-gray-300 text-xs">
                            <div>X: [${this.pointCloudBounds.minX.toFixed(1)}, ${this.pointCloudBounds.maxX.toFixed(1)}]</div>
                            <div>Y: [${this.pointCloudBounds.minY.toFixed(1)}, ${this.pointCloudBounds.maxY.toFixed(1)}]</div>
                            <div>Z: [${this.pointCloudBounds.minZ.toFixed(1)}, ${this.pointCloudBounds.maxZ.toFixed(1)}]</div>
                        </div>
                    </div>
                    
                    <!-- 自适应参数 -->
                    <div class="bg-gray-800/50 rounded-lg p-3">
                        <h4 class="text-teal-400 font-medium mb-2">自适应参数</h4>
                        <div class="grid grid-cols-2 gap-2 text-gray-300">
                            <div>点云限制: <span class="text-white">${Math.round(this.adaptivePointLimit.currentLimit).toLocaleString()}</span></div>
                            <div>距离阈值: <span class="text-white">${this.adaptiveDistanceThreshold.currentThreshold.toFixed(1)}m</span></div>
                        </div>
                    </div>
                    
                    <!-- 轨迹信息 -->
                    <div class="bg-gray-800/50 rounded-lg p-3">
                        <h4 class="text-teal-400 font-medium mb-2">轨迹信息</h4>
                        <div class="grid grid-cols-2 gap-2 text-gray-300">
                            <div>轨迹点数: <span class="text-white">${this.tracePointsData.length}</span></div>
                            <div>交互标记: <span class="text-white">${this.traceMarkers.length}</span></div>
                        </div>
                    </div>
                </div>
                
                <button onclick="document.getElementById('debugModal').remove()" class="btn-primary w-full mt-4 py-2 rounded-lg text-sm">关闭</button>
            </div>
        `;
        document.body.appendChild(modal);
    }

    // 导出点云数据（Shapefile ZIP）
    async exportPointCloud(format) {
        if (!this.currentLake) {
            this.showNotification('请先选择一个湖泊任务', 'error');
            return;
        }

        this.showLoading(true, '正在生成 Shapefile 压缩包...');

        try {
            const response = await fetch(
                `${this.apiBaseUrl}/api/export/points?lake=${encodeURIComponent(this.currentLake)}`
            );

            if (!response.ok) {
                let errMsg = '导出失败';
                try {
                    const err = await response.json();
                    errMsg = err.error || errMsg;
                } catch (_) {
                    errMsg = `服务器错误 (${response.status})`;
                }
                throw new Error(errMsg);
            }

            // 解析文件名
            const disposition = response.headers.get('Content-Disposition');
            let filename = `${this.currentLake}_pointcloud.zip`;
            if (disposition) {
                const match = disposition.match(/filename="(.+)"/);
                if (match) filename = match[1];
            }

            const blob = await response.blob();
            const url = window.URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = filename;
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            window.URL.revokeObjectURL(url);

            this.showNotification(`✅ ${filename} 导出成功`, 'success');
        } catch (error) {
            console.error('导出失败:', error);
            this.showNotification(`导出失败：${error.message}`, 'error');
        } finally {
            this.showLoading(false);
        }
    }

    onWindowResize() {
        const container = document.getElementById('threejs-container');
        if (container && this.camera && this.renderer) {
            this.camera.aspect = container.clientWidth / container.clientHeight;
            this.camera.updateProjectionMatrix();
            this.renderer.setSize(container.clientWidth, container.clientHeight);
        }
        if (this.timeChart) {
            this.timeChart.resize();
        }
    }

    animate() {
        this.animationId = requestAnimationFrame(() => this.animate());
        if (this.controls) this.controls.update();
        if (this.renderer && this.scene && this.camera) {
            this.renderer.render(this.scene, this.camera);
        }
    }

    // ==================== AI助手功能（连接千问API）====================
    
    initAIAssistant() {
        const aiTriggerBtn = document.getElementById('aiTriggerBtn');
        const aiModalOverlay = document.getElementById('aiModalOverlay');
        const aiModalClose = document.getElementById('aiModalClose');
        const aiInput = document.getElementById('aiInput');
        const aiSendBtn = document.getElementById('aiSendBtn');

        if (aiTriggerBtn) {
            aiTriggerBtn.addEventListener('click', () => {
                aiModalOverlay.classList.add('active');
                setTimeout(() => aiInput?.focus(), 300);
            });
        }

        if (aiModalClose) {
            aiModalClose.addEventListener('click', () => {
                aiModalOverlay.classList.remove('active');
            });
        }

        if (aiModalOverlay) {
            aiModalOverlay.addEventListener('click', (e) => {
                if (e.target === aiModalOverlay) {
                    aiModalOverlay.classList.remove('active');
                }
            });
        }

        if (aiInput) {
            aiInput.addEventListener('keypress', (e) => {
                if (e.key === 'Enter' && !this.isAIProcessing) {
                    this.sendAIMessage();
                }
            });
        }

        if (aiSendBtn) {
            aiSendBtn.addEventListener('click', () => {
                if (!this.isAIProcessing) {
                    this.sendAIMessage();
                }
            });
        }

        document.addEventListener('keydown', (e) => {
            if (e.key === 'Escape' && aiModalOverlay?.classList.contains('active')) {
                aiModalOverlay.classList.remove('active');
            }
        });
    }

    // ==================== 调用后端AI API ====================
    async sendAIMessage() {
        const aiInput = document.getElementById('aiInput');
        const message = aiInput?.value?.trim();
        
        if (!message) return;

        aiInput.value = '';
        this.addAIMessage(message, 'user');
        this.addAILoadingMessage();
        this.isAIProcessing = true;

        try {
            // 准备上下文信息
            const lakeStats = await this.fetchLakeStats();
            
            const requestBody = {
                message: message,
                lakeName: this.currentLake,
                sensorData: this.sensorData,
                lakeStats: lakeStats,
                context: this.aiContext.slice(-5) // 最近5轮对话
            };

            console.log('🤖 发送AI请求:', { message: message.substring(0, 50) + '...', lake: this.currentLake });

            const response = await fetch(`${this.apiBaseUrl}/api/ai/chat`, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify(requestBody)
            });

            this.removeAILoadingMessage();

            if (!response.ok) {
                throw new Error(`API请求失败: ${response.status}`);
            }

            const data = await response.json();
            
            if (data.success && data.response) {
                this.addAIMessage(data.response, 'assistant');
                
                // 更新对话上下文
                this.aiContext.push(
                    { role: 'user', content: message },
                    { role: 'assistant', content: data.response }
                );
                
                if (this.aiContext.length > 10) {
                    this.aiContext = this.aiContext.slice(-10);
                }
            } else {
                throw new Error(data.error || 'AI响应为空');
            }

        } catch (error) {
            console.error('❌ AI请求失败:', error);
            this.removeAILoadingMessage();
            
            // 使用备用回复
            const fallbackResponse = this.getAIFallbackResponse(message);
            this.addAIMessage(fallbackResponse, 'assistant');
            
            this.showNotification('AI服务暂时不可用，使用本地备用回复', 'warning');
        } finally {
            this.isAIProcessing = false;
        }
    }
    
    // ============ 新增：获取湖泊统计数据 ============
    async fetchLakeStats() {
        if (!this.currentLake) return null;
        
        try {
            const response = await fetch(`${this.apiBaseUrl}/api/lake-stats?lake=${encodeURIComponent(this.currentLake)}`);
            if (response.ok) {
                return await response.json();
            }
        } catch (e) {
            console.warn('获取湖泊统计失败:', e);
        }
        return null;
    }

    // 快捷提问功能
    askAI(question) {
        const aiInput = document.getElementById('aiInput');
        if (aiInput) {
            aiInput.value = question;
            aiInput.focus();
            setTimeout(() => this.sendAIMessage(), 200);
        }
    }

    addAIMessage(content, type) {
    const aiMessages = document.getElementById('aiMessages');
    if (!aiMessages) return;

    let formatted = content
        .trim()
        // 先把 AI 返回的 markdown 加粗 **文本** 转为 HTML 加粗（避免星号裸露）
        .replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
        .replace(/\\text\{([^}]+)\}/g, '$1')
        .replace(/\\times/g, '×')
        .replace(/\\cdot/g, '×')
        .replace(/\\frac\{([^}]+)\}\{([^}]+)\}/g, '($1)÷($2)')
        .replace(/\$\$?([^\$]+)\$\$?/g, '$1')
        .replace(/\\[a-zA-Z]+/g, '')
        .replace(/^[#\-\*·\+]\s+/gm, '• ')
        .replace(/^(【[^】]+】|[📊🔍💡⚠️✅🌿📋🎯].*)$/gm, '<strong style="color:#2DD4BF;font-size:15px;display:block;margin:8px 0 4px;">$1</strong>')
        .replace(/(\d+\.?\d*\s*(?:°C|m|mg\/L|μS\/cm|NTU|%|mg|g|kg|t|ha|km²))/g, '<span class="data-highlight">$1</span>')
        .replace(/(重要|关键|核心|结论|建议|注意|警告|风险|异常|良好|优秀|差|危险)/g, '<strong style="color:#FDE047;">$1</strong>')
        .replace(/\n{3,}/g, '\n\n')
        .replace(/\n\n/g, '<br><br>')
        .replace(/\n(?!\[•\d<])/g, '<br>');

    const messageDiv = document.createElement('div');
    messageDiv.className = `ai-message ${type}`;
    messageDiv.innerHTML = formatted;
    
    aiMessages.appendChild(messageDiv);
    aiMessages.scrollTop = aiMessages.scrollHeight;
}
    addAILoadingMessage() {
        const aiMessages = document.getElementById('aiMessages');
        if (!aiMessages) return;

        const loadingDiv = document.createElement('div');
        loadingDiv.className = 'ai-message loading';
        loadingDiv.id = 'aiLoadingMessage';
        loadingDiv.innerHTML = `
            <div class="ai-typing-indicator">
                <span></span>
                <span></span>
                <span></span>
            </div>
            <span>AI正在思考...</span>
        `;
        
        aiMessages.appendChild(loadingDiv);
        aiMessages.scrollTop = aiMessages.scrollHeight;
    }

    removeAILoadingMessage() {
        const loadingMsg = document.getElementById('aiLoadingMessage');
        if (loadingMsg) {
            loadingMsg.remove();
        }
    }

    // ==================== 备用 AI 回复 ====================
    getAIFallbackResponse(message) {
        const lowerMsg = message.toLowerCase(); 
        const fmt = (v) => v != null ? Number(v).toFixed(1) : '--';
        
        if (lowerMsg.includes('碳') || lowerMsg.includes('碳汇')) {
            return `🌿 <b>碳汇计算分析</b><br><br>
<b>📊 当前湖泊</b>：${this.currentLake || '未选择'}<br><br>
<b>🔍 关键参数</b>：<br>
• 水深：${fmt(this.sensorData.depth)} m<br>
• 水温：${fmt(this.sensorData.temperature)} °C<br>
• 溶解氧：${fmt(this.sensorData.dissolved_oxygen)} mg/L<br><br>
<b>💡 碳汇估算框架</b>：<br>
碳汇量 = 水域面积 × 单位面积碳储量 × 碳埋藏效率<br><br>
<b>📋 建议</b>：<br>
• 高海拔湖泊碳埋藏效率较高<br>
• 建议结合沉积物采样精确评估`;
        }
        
        return `🤖 <b>AI 助手回复</b><br><br>
<b>📍 当前湖泊</b>：${this.currentLake || '未选择'}<br>
<b>📡 数据状态</b>：${this.hasValidSensorData ? '有效' : '无有效数据'}<br><br>
<b>📊 最新传感器数据</b>：<br>
• 深度：${fmt(this.sensorData.depth)} m<br>
• 温度：${fmt(this.sensorData.temperature)} °C<br>
• 溶解氧：${fmt(this.sensorData.dissolved_oxygen)} mg/L<br>
• pH：${fmt(this.sensorData.ph)}<br>
• 电导率：${fmt(this.sensorData.conductivity)} μS/cm<br>
• 浊度：${fmt(this.sensorData.turbidity)} NTU`;
    }
}

// 初始化应用
const app = new BlueRidgeSystem();
