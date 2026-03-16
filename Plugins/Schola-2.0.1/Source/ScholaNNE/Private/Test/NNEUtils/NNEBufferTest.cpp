// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "NNEUtils/NNEBuffer.h"
#include "NNEUtils/NNEBufferVisitor.h"

#define TestEqualExactFloat(TestMessage, Actual, Expected) TestEqual(TestMessage, (float)Actual, (float)Expected, 0.0001f)

// Minimal visitor for core behavior checks
struct FTestBufferVisitor : public FNNEBufferVisitor
{
	const FNNEDiscreteBuffer* DiscreteRef = nullptr;
	const FNNEMultiDiscreteBuffer*	 MultiDiscreteRef = nullptr;
	const FNNEBoxBuffer* BoxRef = nullptr;
	const FNNEMultiBinaryBuffer* BinaryRef = nullptr;
	const FNNEDictBuffer* DictRef = nullptr;
	TArray<float> CopiedData;

	void operator()(const FNNEDiscreteBuffer& InBuffer) override
	{
		DiscreteRef = &InBuffer;
		CopiedData = InBuffer.Buffer;
	}
	void operator()(const FNNEBoxBuffer& InBuffer) override
	{
		BoxRef = &InBuffer;
		CopiedData = InBuffer.Buffer;
	}
	void operator()(const FNNEMultiBinaryBuffer& InBuffer) override
	{
		BinaryRef = &InBuffer;
		CopiedData = InBuffer.Buffer;
	}
	void operator()(const FNNEDictBuffer& InBuffer) override
	{
		DictRef = &InBuffer;
	}

	void operator()(const FNNEMultiDiscreteBuffer& InBuffer) override
	{
		MultiDiscreteRef = &InBuffer;
		CopiedData = InBuffer.Buffer;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNNEStateBufferBasicsTest, "Schola.Policies.NNE.NNEBuffer.StateBuffer Basics", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNNEStateBufferBasicsTest::RunTest(const FString& Parameters)
{
	// 3 sequences, dim=2
	FNNEStateBuffer StateBuffer(3, 2);

	// Fill with rows: [1,2] [3,4] [5,6]
	StateBuffer.StateBuffer[0] = 1.0f;
	StateBuffer.StateBuffer[1] = 2.0f;
	StateBuffer.StateBuffer[2] = 3.0f;
	StateBuffer.StateBuffer[3] = 4.0f;
	StateBuffer.StateBuffer[4] = 5.0f;
	StateBuffer.StateBuffer[5] = 6.0f;

	// Shift should drop first row and repeat last row
	StateBuffer.Shift();
	TestEqualExactFloat(TEXT("After shift: seq0_dim0 == 3"), StateBuffer.StateBuffer[0], 3.0f);
	TestEqualExactFloat(TEXT("After shift: seq0_dim1 == 4"), StateBuffer.StateBuffer[1], 4.0f);
	TestEqualExactFloat(TEXT("After shift: seq1_dim0 == 5"), StateBuffer.StateBuffer[2], 5.0f);
	TestEqualExactFloat(TEXT("After shift: seq1_dim1 == 6"), StateBuffer.StateBuffer[3], 6.0f);
	TestEqualExactFloat(TEXT("After shift: seq2_dim0 unchanged == 5"), StateBuffer.StateBuffer[4], 5.0f);
	TestEqualExactFloat(TEXT("After shift: seq2_dim1 unchanged == 6"), StateBuffer.StateBuffer[5], 6.0f);

	// Bindings
	UE::NNE::FTensorBindingCPU InputBinding = StateBuffer.MakeInputBinding();
	TestEqual(TEXT("Input binding size"), InputBinding.SizeInBytes, 3 * 2 * sizeof(float));
	TestEqual(TEXT("Input binding data"), InputBinding.Data, (void*)StateBuffer.StateBuffer.GetData());

	UE::NNE::FTensorBindingCPU OutputBinding = StateBuffer.MakeOutputBinding();
	TestEqual(TEXT("Output binding size"), OutputBinding.SizeInBytes, 2 * sizeof(float));
	void* ExpectedOutputPtr = (void*)(StateBuffer.StateBuffer.GetData() + (3 - 1) * 2);
	TestEqual(TEXT("Output binding data points to last row"), OutputBinding.Data, ExpectedOutputPtr);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNNEDiscreteBufferBasicsTest, "Schola.Policies.NNE.NNEBuffer.DiscreteBuffer Basics", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNNEDiscreteBufferBasicsTest::RunTest(const FString& Parameters)
{
	FNNEDiscreteBuffer Buffer(7);
	Buffer.Buffer[0] = 0.1f;
	Buffer.Buffer[3] = 0.8f;
	Buffer.Buffer[6] = 0.2f;

	// Visitor
	FTestBufferVisitor Visitor;
	Buffer.Accept(Visitor);
	TestTrue(TEXT("Visitor captured DiscreteBuffer"), Visitor.DiscreteRef != nullptr);
	TestEqual(TEXT("Visitor copied size"), Visitor.CopiedData.Num(), 7);
	TestEqualExactFloat(TEXT("Visitor copy matches [0]"), Visitor.CopiedData[0], 0.1f);
	TestEqualExactFloat(TEXT("Visitor copy matches [3]"), Visitor.CopiedData[3], 0.8f);
	TestEqualExactFloat(TEXT("Visitor copy matches [6]"), Visitor.CopiedData[6], 0.2f);

	// Binding
	UE::NNE::FTensorBindingCPU Binding = Buffer.MakeBinding();
	TestEqual(TEXT("Binding size"), Binding.SizeInBytes, 7 * sizeof(float));
	TestEqual(TEXT("Binding data ptr"), Binding.Data, (void*)Buffer.Buffer.GetData());
	float* BindingData = static_cast<float*>(Binding.Data);
	TestEqualExactFloat(TEXT("Binding data mirrors buffer [3]"), BindingData[3], 0.8f);

	// Copy independence
	FNNEDiscreteBuffer Copy = Buffer;
	Copy.Buffer[0] = 9.9f;
	TestEqualExactFloat(TEXT("Copy mutated [0]"), Copy.Buffer[0], 9.9f);
	TestEqualExactFloat(TEXT("Original unaffected [0]"), Buffer.Buffer[0], 0.1f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNNEBoxAndBinaryBufferTest, "Schola.Policies.NNE.NNEBuffer.BoxAndBinary Basics", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNNEBoxAndBinaryBufferTest::RunTest(const FString& Parameters)
{
	// Box
	{
		FNNEBoxBuffer Box(5);
		for (int i = 0; i < 5; ++i)
		{
			TestEqualExactFloat(TEXT("Box init == 0"), Box.Buffer[i], 0.0f);
		}
		Box.Buffer[0] = 1.5f;
		Box.Buffer[2] = -2.7f;
		Box.Buffer[4] = 3.14f;
		TestEqualExactFloat(TEXT("Box[0]"), Box.Buffer[0], 1.5f);
		TestEqualExactFloat(TEXT("Box[2]"), Box.Buffer[2], -2.7f);
		TestEqualExactFloat(TEXT("Box[4]"), Box.Buffer[4], 3.14f);

		UE::NNE::FTensorBindingCPU Binding = Box.MakeBinding();
		TestEqual(TEXT("Box binding size"), Binding.SizeInBytes, 5 * sizeof(float));
		TestEqual(TEXT("Box binding ptr"), Binding.Data, (void*)Box.Buffer.GetData());
	}

	// Binary
	{
		FNNEMultiBinaryBuffer Binary(4);
		Binary.Buffer[0] = 1.0f;
		Binary.Buffer[1] = 0.0f;
		Binary.Buffer[2] = 1.0f;
		Binary.Buffer[3] = 0.0f;

		UE::NNE::FTensorBindingCPU Binding = Binary.MakeBinding();
		TestEqual(TEXT("Binary binding size"), Binding.SizeInBytes, 4 * sizeof(float));
		TestEqual(TEXT("Binary binding ptr"), Binding.Data, (void*)Binary.Buffer.GetData());
		TestEqualExactFloat(TEXT("Binary[0]"), Binary.Buffer[0], 1.0f);
		TestEqualExactFloat(TEXT("Binary[1]"), Binary.Buffer[1], 0.0f);
		TestEqualExactFloat(TEXT("Binary[2]"), Binary.Buffer[2], 1.0f);
		TestEqualExactFloat(TEXT("Binary[3]"), Binary.Buffer[3], 0.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNNEDictBufferBasicTest, "Schola.Policies.NNE.NNEBuffer.DictBuffer Basics", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNNEDictBufferBasicTest::RunTest(const FString& Parameters)
{
	TInstancedStruct<FNNEPointBuffer> Pos;
	Pos.InitializeAs<FNNEBoxBuffer>(3);
	Pos.GetMutable<FNNEBoxBuffer>().Buffer[0] = 1.0f;
	Pos.GetMutable<FNNEBoxBuffer>().Buffer[1] = 2.0f;
	Pos.GetMutable<FNNEBoxBuffer>().Buffer[2] = 3.0f;

	TInstancedStruct<FNNEPointBuffer> Act;
	Act.InitializeAs<FNNEDiscreteBuffer>(2);
	Act.GetMutable<FNNEDiscreteBuffer>().Buffer[0] = 0.7f;
	Act.GetMutable<FNNEDiscreteBuffer>().Buffer[1] = 0.3f;

	TMap<FString, TInstancedStruct<FNNEPointBuffer>> Map;
	Map.Add("position", Pos);
	Map.Add("action", Act);

	FNNEDictBuffer Dict(Map);
	TestEqual(TEXT("Entry count"), Dict.Buffers.Num(), 2);
	TestTrue(TEXT("Has position"), Dict.Buffers.Contains("position"));
	TestTrue(TEXT("Has action"), Dict.Buffers.Contains("action"));

	const FNNEBoxBuffer& PosRef = Dict.Buffers["position"].Get<FNNEBoxBuffer>();
	TestEqualExactFloat(TEXT("Pos[0]"), PosRef.Buffer[0], 1.0f);
	TestEqualExactFloat(TEXT("Pos[1]"), PosRef.Buffer[1], 2.0f);
	TestEqualExactFloat(TEXT("Pos[2]"), PosRef.Buffer[2], 3.0f);

	const FNNEDiscreteBuffer& ActRef = Dict.Buffers["action"].Get<FNNEDiscreteBuffer>();
	TestEqualExactFloat(TEXT("Act[0]"), ActRef.Buffer[0], 0.7f);
	TestEqualExactFloat(TEXT("Act[1]"), ActRef.Buffer[1], 0.3f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNNEDiscreteBufferInstancedStructTest, "Schola.Policies.NNE.NNEBuffer.DiscreteBuffer InstancedStruct", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNNEDiscreteBufferInstancedStructTest::RunTest(const FString& Parameters)
{
	TInstancedStruct<FNNEPointBuffer> BufferInstance;
	BufferInstance.InitializeAs<FNNEDiscreteBuffer>(4);

	FNNEDiscreteBuffer& Discrete = BufferInstance.GetMutable<FNNEDiscreteBuffer>();
	Discrete.Buffer[0] = 11.0f;
	Discrete.Buffer[1] = 22.0f;
	Discrete.Buffer[2] = 33.0f;
	Discrete.Buffer[3] = 44.0f;

	FTestBufferVisitor Visitor;
	BufferInstance.Get<FNNEPointBuffer>().Accept(Visitor);
	TestTrue(TEXT("Visitor got Discrete"), Visitor.DiscreteRef != nullptr);
	TestEqual(TEXT("Visitor size"), Visitor.CopiedData.Num(), 4);
	TestEqualExactFloat(TEXT("Visitor[0]"), Visitor.CopiedData[0], 11.0f);
	TestEqualExactFloat(TEXT("Visitor[3]"), Visitor.CopiedData[3], 44.0f);

	const FNNEDiscreteBuffer& ConstBuffer = BufferInstance.Get<FNNEDiscreteBuffer>();
	UE::NNE::FTensorBindingCPU Binding = ConstBuffer.MakeBinding();
	TestEqual(TEXT("Binding ptr"), Binding.Data, (void*)ConstBuffer.Buffer.GetData());
	TestEqual(TEXT("Binding size"), Binding.SizeInBytes, 4 * sizeof(float));
	return true;
}